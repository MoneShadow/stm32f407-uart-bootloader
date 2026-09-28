"""STM32F407 Bootloader 的 PC 端 UART 协议与固件升级工具。"""

import argparse
import binascii
import math
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


SOF = b"\xA5\x5A"
VERSION = 0x01
MAX_PAYLOAD_SIZE = 256
# 必须与 Bootloader 的 STM32F407xx_FLASH.ld 分区保持一致；末尾 Sector 11 留给元数据。
APP_FLASH_START = 0x08008000
APP_FLASH_END = 0x080E0000
SRAM_START, SRAM_END = 0x20000000, 0x20020000
CCM_RAM_START, CCM_RAM_END = 0x10000000, 0x10010000

COMMAND_HELLO = 0x01
COMMAND_START = 0x02
COMMAND_DATA = 0x03
COMMAND_END = 0x04
COMMAND_ACK = 0x80
COMMAND_NACK = 0x81

_FIXED_PAYLOAD_LENGTHS = {
    COMMAND_HELLO: 0,
    COMMAND_START: 12,
    COMMAND_END: 0,
    COMMAND_ACK: 1,
    COMMAND_NACK: 2,
}


@dataclass(frozen=True)
class Frame:
    """通过长度、CRC 和命令格式检查后的协议帧。"""

    command: int
    sequence: int
    payload: bytes


@dataclass(frozen=True)
class FirmwareImage:
    data: bytes
    version: int
    crc32: int


class InvalidFrameError(ValueError):
    """收到的字节不构成有效协议帧。"""


class NackError(RuntimeError):
    """Bootloader 明确拒绝了一个请求。"""

    def __init__(self, command: int, sequence: int, reason: int) -> None:
        self.command = command
        self.sequence = sequence
        self.reason = reason
        super().__init__(
            f"NACK for command 0x{command:02X}, sequence {sequence}, reason 0x{reason:02X}"
        )


def _check_payload_length(command: int, payload_length: int) -> None:
    if command == COMMAND_DATA:
        if not 1 <= payload_length <= MAX_PAYLOAD_SIZE:
            raise ValueError("DATA payload must contain 1 to 256 bytes")
    elif command in _FIXED_PAYLOAD_LENGTHS:
        if payload_length != _FIXED_PAYLOAD_LENGTHS[command]:
            raise ValueError("invalid payload length for command")
    else:
        raise ValueError("unknown command")


def encode_frame(command: int, sequence: int, payload: bytes = b"") -> bytes:
    """按 A5 5A | VERSION | CMD | SEQ | LEN | PAYLOAD | CRC16 编码一帧。"""
    if not isinstance(command, int) or not 0 <= command <= 0xFF:
        raise ValueError("command must be an unsigned 8-bit integer")
    if not isinstance(sequence, int) or not 0 <= sequence <= 0xFFFF:
        raise ValueError("sequence must be an unsigned 16-bit integer")
    if not isinstance(payload, bytes):
        raise TypeError("payload must be bytes")

    payload_length = len(payload)
    _check_payload_length(command, payload_length)

    # CRC 从 VERSION 算到 PAYLOAD，不包括 SOF 和 CRC 字段本身。
    body = struct.pack("<BBHH", VERSION, command, sequence, payload_length) + payload
    crc = binascii.crc_hqx(body, 0xFFFF)
    return SOF + body + struct.pack("<H", crc)


def decode_frame(encoded: bytes) -> Frame:
    """验证一段完整的编码帧，成功后返回解析结果。"""
    if not isinstance(encoded, bytes):
        raise TypeError("encoded frame must be bytes")
    if not 10 <= len(encoded) <= 10 + MAX_PAYLOAD_SIZE:
        raise InvalidFrameError("invalid frame size")
    if encoded[:2] != SOF:
        raise InvalidFrameError("invalid SOF")

    version, command, sequence, payload_length = struct.unpack_from("<BBHH", encoded, 2)
    if payload_length > MAX_PAYLOAD_SIZE or len(encoded) != 10 + payload_length:
        raise InvalidFrameError("invalid payload length")

    payload_end = 8 + payload_length
    received_crc = struct.unpack_from("<H", encoded, payload_end)[0]
    calculated_crc = binascii.crc_hqx(encoded[2:payload_end], 0xFFFF)
    if received_crc != calculated_crc:
        raise InvalidFrameError("CRC16 mismatch")
    if version != VERSION:
        raise InvalidFrameError("unsupported version")
    try:
        _check_payload_length(command, payload_length)
    except ValueError as exc:
        raise InvalidFrameError(str(exc)) from exc

    return Frame(command, sequence, encoded[8:payload_end])


def _read_exact(port, length: int, deadline: float) -> bytes:
    data = bytearray()
    while len(data) < length:
        if time.monotonic() >= deadline:
            raise TimeoutError("timed out while receiving a frame")
        chunk = port.read(length - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def _read_frame_until(port, deadline: float) -> Frame:
    saw_first_sof_byte = False
    while True:
        byte = _read_exact(port, 1, deadline)[0]
        if not saw_first_sof_byte:
            saw_first_sof_byte = byte == SOF[0]
        elif byte == SOF[1]:
            break
        else:
            # A5 A5 5A 中的第二个 A5 也可以是帧头起点。
            saw_first_sof_byte = byte == SOF[0]

    header = _read_exact(port, 6, deadline)
    payload_length = struct.unpack_from("<H", header, 4)[0]
    if payload_length > MAX_PAYLOAD_SIZE:
        raise InvalidFrameError("payload length exceeds 256 bytes")
    tail = _read_exact(port, payload_length + 2, deadline)
    return decode_frame(SOF + header + tail)


def read_frame(port, timeout_s: float) -> Frame:
    """从串口字节流中接收一帧；port.read() 必须设置有限的短超时。"""
    if timeout_s <= 0:
        raise ValueError("timeout must be positive")
    return _read_frame_until(port, time.monotonic() + timeout_s)


def wait_for_reply(port, command: int, sequence: int, timeout_s: float) -> Frame:
    """等待对应请求的 ACK；匹配的 NACK 抛出 NackError。"""
    if command not in (COMMAND_HELLO, COMMAND_START, COMMAND_DATA, COMMAND_END):
        raise ValueError("expected command must be a request command")
    if not isinstance(sequence, int) or not 0 <= sequence <= 0xFFFF:
        raise ValueError("sequence must be an unsigned 16-bit integer")
    if timeout_s <= 0:
        raise ValueError("timeout must be positive")

    deadline = time.monotonic() + timeout_s
    while True:
        try:
            frame = _read_frame_until(port, deadline)
        except InvalidFrameError:
            # 噪声、损坏帧和无关帧都不能延长本次请求的总等待时间。
            continue
        if frame.sequence != sequence or frame.command not in (COMMAND_ACK, COMMAND_NACK):
            continue
        if frame.payload[0] != command:
            continue
        if frame.command == COMMAND_NACK:
            raise NackError(command, sequence, frame.payload[1])
        return frame


def probe_hello(port, timeout_s: float, retry_interval_s: float = 0.25) -> Frame:
    """在总期限内重复发送 HELLO(序号 0)，直到收到匹配的 ACK。"""
    if not math.isfinite(timeout_s) or timeout_s <= 0:
        raise ValueError("overall timeout must be finite and positive")
    if not math.isfinite(retry_interval_s) or retry_interval_s <= 0:
        raise ValueError("retry interval must be finite and positive")

    hello = encode_frame(COMMAND_HELLO, 0)
    deadline = time.monotonic() + timeout_s
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("no HELLO ACK received before the overall deadline")
        if port.write(hello) != len(hello):
            raise OSError("incomplete HELLO write")
        try:
            return wait_for_reply(port, COMMAND_HELLO, 0, min(retry_interval_s, remaining))
        except TimeoutError:
            # 可能尚未复位进入 Bootloader；继续发同一个 HELLO，不改变序号。
            pass


def prepare_firmware(data: bytes, version: int) -> FirmwareImage:
    """在任何串口操作之前，按板端规则检查原始 .bin 镜像。"""
    if not isinstance(data, bytes):
        raise TypeError("firmware data must be bytes")
    if not isinstance(version, int) or not 0 <= version <= 0xFFFFFFFF:
        raise ValueError("firmware version must be an unsigned 32-bit integer")
    if not 8 <= len(data) <= APP_FLASH_END - APP_FLASH_START:
        raise ValueError("firmware size is outside the APP partition")

    initial_msp, reset_handler = struct.unpack_from("<II", data)
    valid_stack = (SRAM_START < initial_msp <= SRAM_END or
                   CCM_RAM_START < initial_msp <= CCM_RAM_END)
    if initial_msp & 7 or not valid_stack:
        raise ValueError("firmware initial MSP is invalid")
    reset_address = reset_handler & ~1
    if not reset_handler & 1 or not APP_FLASH_START <= reset_address < APP_FLASH_START + len(data):
        raise ValueError("firmware Reset_Handler must be Thumb and inside the image")

    return FirmwareImage(data, version, binascii.crc32(data) & 0xFFFFFFFF)


def load_firmware(path: Path, version: int) -> FirmwareImage:
    return prepare_firmware(path.read_bytes(), version)


def _send_request(port, command: int, sequence: int, payload: bytes, timeout_s: float) -> Frame:
    encoded = encode_frame(command, sequence, payload)
    if port.write(encoded) != len(encoded):
        raise OSError(f"incomplete write for command 0x{command:02X}, sequence {sequence}")
    return wait_for_reply(port, command, sequence, timeout_s)


def send_firmware(
    port,
    image: FirmwareImage,
    hello_timeout_s: float = 10.0,
    start_timeout_s: float = 15.0,
    data_timeout_s: float = 1.0,
    end_timeout_s: float = 10.0,
    data_retries: int = 2,
    progress: Callable[[int, int], None] | None = None,
) -> None:
    """逐帧升级；只有 DATA 超时允许原包重传。"""
    if any(not math.isfinite(value) or value <= 0 for value in
           (hello_timeout_s, start_timeout_s, data_timeout_s, end_timeout_s)):
        raise ValueError("timeouts must be finite and positive")
    if not isinstance(data_retries, int) or data_retries < 0:
        raise ValueError("data_retries must be non-negative")

    probe_hello(port, hello_timeout_s)
    start_payload = struct.pack("<III", len(image.data), image.crc32, image.version)
    try:
        _send_request(port, COMMAND_START, 1, start_payload, start_timeout_s)
    except TimeoutError as exc:
        raise TimeoutError("START ACK 未确认；设备可能已擦除 APP，请勿假定旧程序仍可运行") from exc

    sequence = 2
    for offset in range(0, len(image.data), MAX_PAYLOAD_SIZE):
        chunk = image.data[offset:offset + MAX_PAYLOAD_SIZE]
        for attempt in range(data_retries + 1):
            try:
                _send_request(port, COMMAND_DATA, sequence, chunk, data_timeout_s)
                break
            except TimeoutError as exc:
                if attempt == data_retries:
                    raise TimeoutError(f"DATA 序号 {sequence} 无 ACK；升级未完成") from exc
                # 重发完全相同的序号和数据；板端会识别已写入的重复包。
        sequence += 1
        if progress is not None:
            progress(offset + len(chunk), len(image.data))

    try:
        _send_request(port, COMMAND_END, sequence, b"", end_timeout_s)
    except TimeoutError as exc:
        raise TimeoutError("END ACK 未确认；设备可能已提交并复位，需重新检查而不是重发 END") from exc


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="默认只验证 HELLO；提供 .bin 后先检查，明确加 --upgrade 才会擦写板子")
    parser.add_argument("--port", help="串口设备，例如 /dev/ttyUSB0 或 COM3")
    parser.add_argument("--wait", type=float, default=10.0, help="等待板子复位并应答的总秒数，默认 10")
    parser.add_argument("--image", type=Path, help="应用程序原始 .bin 文件；不加 --upgrade 时只做本地检查")
    parser.add_argument("--firmware-version", type=lambda value: int(value, 0), help="写入元数据的版本号，可用 1 或 0x1")
    parser.add_argument("--upgrade", action="store_true", help="明确允许发送 START，擦除并更新板上 APP")
    args = parser.parse_args(argv)
    if not math.isfinite(args.wait) or args.wait <= 0:
        parser.error("--wait must be finite and positive")
    if args.upgrade and args.image is None:
        parser.error("--upgrade requires --image")
    if args.image is not None and args.firmware_version is None:
        parser.error("--image requires --firmware-version")
    if args.image is None and args.firmware_version is not None:
        parser.error("--firmware-version requires --image")
    if (args.upgrade or args.image is None) and not args.port:
        parser.error("serial operation requires --port")

    image = None
    if args.image is not None:
        try:
            image = load_firmware(args.image, args.firmware_version)
        except (OSError, TypeError, ValueError) as exc:
            print(f"镜像检查失败：{exc}", file=sys.stderr)
            return 1
        print(f"镜像：{args.image}，{len(image.data)} 字节，CRC32=0x{image.crc32:08X}，版本={image.version}")
        if not args.upgrade:
            print("仅本地检查，未连接串口；加 --upgrade 才会擦写板子。")
            return 0

    try:
        import serial  # pyserial；协议单元测试不依赖它
    except ImportError:
        print("缺少 pyserial，请先运行：python3 -m pip install pyserial", file=sys.stderr)
        return 2

    try:
        with serial.Serial(args.port, baudrate=115200, timeout=0.02, write_timeout=0.2) as port:
            port.reset_input_buffer()  # 避免把上次残留的 ACK 当作本次的应答。
            print(f"已打开 {args.port} (115200 8N1)。请在 {args.wait:g} 秒内按板子 Reset 键。")
            if image is None:
                print(f"反复发送 HELLO：{encode_frame(COMMAND_HELLO, 0).hex(' ').upper()}")
                reply = probe_hello(port, args.wait)
            else:
                print("收到 HELLO ACK 后将发送 START；板端会擦除原 APP 和元数据。")
                last_reported = 0

                def show_progress(sent: int, total: int) -> None:
                    nonlocal last_reported
                    if sent == total or sent - last_reported >= 4096:
                        print(f"DATA 已确认：{sent}/{total} 字节")
                        last_reported = sent

                send_firmware(port, image, hello_timeout_s=args.wait, progress=show_progress)
    except (serial.SerialException, OSError, TimeoutError, NackError) as exc:
        print(f"串口操作失败：{exc}", file=sys.stderr)
        return 1

    if image is None:
        print(f"收到有效 ACK：{encode_frame(reply.command, reply.sequence, reply.payload).hex(' ').upper()}")
    else:
        print("收到 END ACK；板端正在复位，新 APP 将由 Bootloader 校验后启动。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
