"""PC 端编码结果与 Bootloader 协议黄金字节对照。"""

import binascii
import struct
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from tools.upgrade import (
    COMMAND_ACK,
    COMMAND_DATA,
    COMMAND_END,
    COMMAND_HELLO,
    COMMAND_START,
    Frame,
    InvalidFrameError,
    NackError,
    decode_frame,
    encode_frame,
    load_firmware,
    main,
    prepare_firmware,
    probe_hello,
    read_frame,
    send_firmware,
    wait_for_reply,
)


class FakeSerial:
    """每次最多返回两个字节，用于模拟串口分段到达。"""

    def __init__(self, data: bytes) -> None:
        self.data = bytearray(data)
        self.writes: list[bytes] = []

    def read(self, size: int) -> bytes:
        count = min(size, 2, len(self.data))
        chunk = bytes(self.data[:count])
        del self.data[:count]
        return chunk

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)


class EncodeFrameTests(unittest.TestCase):
    def test_hello(self) -> None:
        self.assertEqual(
            encode_frame(COMMAND_HELLO, 0),
            bytes.fromhex("A5 5A 01 01 00 00 00 00 E1 E1"),
        )

    def test_start_little_endian_fields(self) -> None:
        payload = struct.pack("<III", 8, 0x9118E1C2, 1)
        self.assertEqual(
            encode_frame(COMMAND_START, 1, payload),
            bytes.fromhex(
                "A5 5A 01 02 01 00 0C 00 "
                "08 00 00 00 C2 E1 18 91 01 00 00 00 C0 F8"
            ),
        )

    def test_data(self) -> None:
        self.assertEqual(
            encode_frame(COMMAND_DATA, 2, bytes.fromhex("11 22 33 44")),
            bytes.fromhex("A5 5A 01 03 02 00 04 00 11 22 33 44 0C F7"),
        )

    def test_maximum_data_payload_and_sequence_byte_order(self) -> None:
        frame = encode_frame(COMMAND_DATA, 0x1234, bytes(range(256)))
        self.assertEqual(len(frame), 266)
        self.assertEqual(frame[4:8], bytes.fromhex("34 12 00 01"))

    def test_invalid_fields(self) -> None:
        invalid_frames = [
            (COMMAND_HELLO, 0, b"x"),
            (COMMAND_START, 1, b""),
            (COMMAND_DATA, 2, b""),
            (COMMAND_DATA, 2, bytes(257)),
            (COMMAND_DATA, 0x10000, b"x"),
            (0x7F, 0, b""),
        ]
        for command, sequence, payload in invalid_frames:
            with self.subTest(command=command, sequence=sequence, length=len(payload)):
                with self.assertRaises(ValueError):
                    encode_frame(command, sequence, payload)

        with self.assertRaises(TypeError):
            encode_frame(COMMAND_DATA, 2, bytearray(b"x"))


class ReceiveReplyTests(unittest.TestCase):
    HELLO_ACK = bytes.fromhex("A5 5A 01 80 00 00 01 00 01 9E BA")

    def test_decode_hello_ack(self) -> None:
        self.assertEqual(
            decode_frame(self.HELLO_ACK),
            Frame(COMMAND_ACK, 0, bytes([COMMAND_HELLO])),
        )

    def test_decode_rejects_bad_crc_and_truncated_frame(self) -> None:
        bad_crc = self.HELLO_ACK[:-1] + bytes([self.HELLO_ACK[-1] ^ 1])
        for raw in (bad_crc, self.HELLO_ACK[:-1]):
            with self.subTest(raw=raw):
                with self.assertRaises(InvalidFrameError):
                    decode_frame(raw)

    def test_decode_rejects_invalid_version_and_command_length(self) -> None:
        wrong_version_body = b"\x02\x80\x00\x00\x01\x00\x01"
        wrong_version = b"\xA5\x5A" + wrong_version_body + struct.pack(
            "<H", binascii.crc_hqx(wrong_version_body, 0xFFFF)
        )
        invalid_length = encode_frame(COMMAND_DATA, 2, b"\x01")
        invalid_length = invalid_length[:3] + bytes([COMMAND_HELLO]) + invalid_length[4:-2]
        invalid_length += struct.pack("<H", binascii.crc_hqx(invalid_length[2:], 0xFFFF))
        for raw in (wrong_version, invalid_length):
            with self.subTest(raw=raw):
                with self.assertRaises(InvalidFrameError):
                    decode_frame(raw)

    def test_read_frame_skips_noise_and_handles_partial_reads(self) -> None:
        port = FakeSerial(b"\x00\xA5" + self.HELLO_ACK)
        self.assertEqual(read_frame(port, 0.1), Frame(COMMAND_ACK, 0, b"\x01"))

    def test_wait_for_reply_skips_damaged_and_stale_frames(self) -> None:
        bad_crc = self.HELLO_ACK[:-1] + bytes([self.HELLO_ACK[-1] ^ 1])
        stale = encode_frame(COMMAND_ACK, 1, bytes([COMMAND_DATA]))
        wrong_command = encode_frame(COMMAND_ACK, 2, bytes([COMMAND_HELLO]))
        expected = encode_frame(COMMAND_ACK, 2, bytes([COMMAND_DATA]))
        port = FakeSerial(bad_crc + stale + wrong_command + expected)
        self.assertEqual(wait_for_reply(port, COMMAND_DATA, 2, 0.1), Frame(COMMAND_ACK, 2, b"\x03"))

    def test_wait_for_reply_reports_nack_reason(self) -> None:
        nack = bytes.fromhex("A5 5A 01 81 02 00 02 00 03 05 AB 06")
        with self.assertRaises(NackError) as caught:
            wait_for_reply(FakeSerial(nack), COMMAND_DATA, 2, 0.1)
        self.assertEqual(caught.exception.reason, 0x05)

    def test_wait_for_reply_has_a_total_timeout(self) -> None:
        with self.assertRaises(TimeoutError):
            wait_for_reply(FakeSerial(b""), COMMAND_HELLO, 0, 0.005)

    def test_probe_retries_identical_hello_until_ack(self) -> None:
        class ReplyAfterThreeWrites(FakeSerial):
            def write(self, data: bytes) -> int:
                result = super().write(data)
                if len(self.writes) == 3:
                    self.data.extend(ReceiveReplyTests.HELLO_ACK)
                return result

        port = ReplyAfterThreeWrites(b"")
        self.assertEqual(probe_hello(port, 0.1, 0.005), Frame(COMMAND_ACK, 0, b"\x01"))
        self.assertEqual(port.writes, [encode_frame(COMMAND_HELLO, 0)] * 3)

    def test_probe_stops_at_overall_deadline(self) -> None:
        port = FakeSerial(b"")
        with self.assertRaises(TimeoutError):
            probe_hello(port, 0.01, 0.005)
        self.assertGreaterEqual(len(port.writes), 1)

    def test_probe_rejects_nonfinite_timeout(self) -> None:
        with self.assertRaises(ValueError):
            probe_hello(FakeSerial(b""), float("nan"))

    def test_probe_does_not_retry_matching_nack(self) -> None:
        nack = encode_frame(0x81, 0, b"\x01\x06")
        port = FakeSerial(nack)
        with self.assertRaises(NackError):
            probe_hello(port, 0.1)
        self.assertEqual(port.writes, [encode_frame(COMMAND_HELLO, 0)])


class UpgradeTests(unittest.TestCase):
    @staticmethod
    def sample_image() -> bytes:
        return struct.pack("<II", 0x20020000, 0x08008009) + bytes(range(253))

    def test_prepare_firmware_checks_vectors_and_crc32(self) -> None:
        data = self.sample_image()
        image = prepare_firmware(data, 7)
        self.assertEqual((len(image.data), image.version, image.crc32),
                         (261, 7, binascii.crc32(data)))
        with self.assertRaises(ValueError):
            prepare_firmware(b"\x7FELF" + bytes(20), 7)
        with self.assertRaises(ValueError):
            prepare_firmware(data, -1)
        outside_image = struct.pack("<II", 0x20020000, 0x08009001) + data[8:]
        with self.assertRaises(ValueError):
            prepare_firmware(outside_image, 7)

    def test_check_only_cli_does_not_need_serial_port(self) -> None:
        with TemporaryDirectory() as directory:
            path = Path(directory) / "app.bin"
            path.write_bytes(self.sample_image())
            self.assertEqual(main(["--image", str(path), "--firmware-version", "7"]), 0)
            self.assertEqual(load_firmware(path, 7).version, 7)

    def test_full_upgrade_sends_expected_frames_and_retries_data_only(self) -> None:
        class ReplyingSerial(FakeSerial):
            def __init__(self) -> None:
                super().__init__(b"")
                self.dropped_data_ack = False

            def write(self, data: bytes) -> int:
                result = super().write(data)
                frame = decode_frame(data)
                if frame.command == COMMAND_DATA and not self.dropped_data_ack:
                    self.dropped_data_ack = True
                else:
                    self.data.extend(encode_frame(COMMAND_ACK, frame.sequence, bytes([frame.command])))
                return result

        port = ReplyingSerial()
        image = prepare_firmware(self.sample_image(), 7)
        confirmed = []
        send_firmware(port, image, hello_timeout_s=0.1, start_timeout_s=0.1,
                      data_timeout_s=0.005, end_timeout_s=0.1,
                      progress=lambda sent, total: confirmed.append((sent, total)))
        frames = [decode_frame(raw) for raw in port.writes]
        self.assertEqual([(frame.command, frame.sequence) for frame in frames], [
            (COMMAND_HELLO, 0), (COMMAND_START, 1),
            (COMMAND_DATA, 2), (COMMAND_DATA, 2), (COMMAND_DATA, 3), (COMMAND_END, 4),
        ])
        self.assertEqual(frames[1].payload, struct.pack("<III", 261, image.crc32, 7))
        self.assertEqual(frames[2].payload + frames[4].payload, image.data)
        self.assertEqual(port.writes[2], port.writes[3])
        self.assertEqual(confirmed, [(256, 261), (261, 261)])

    def test_start_timeout_never_sends_data(self) -> None:
        class HelloOnlySerial(FakeSerial):
            def write(self, data: bytes) -> int:
                result = super().write(data)
                frame = decode_frame(data)
                if frame.command == COMMAND_HELLO:
                    self.data.extend(encode_frame(COMMAND_ACK, 0, b"\x01"))
                return result

        port = HelloOnlySerial(b"")
        with self.assertRaisesRegex(TimeoutError, "START ACK 未确认"):
            send_firmware(port, prepare_firmware(self.sample_image(), 7),
                          start_timeout_s=0.005)
        self.assertEqual([decode_frame(raw).command for raw in port.writes],
                         [COMMAND_HELLO, COMMAND_START])

    def test_end_timeout_does_not_retry_end(self) -> None:
        class NoEndReplySerial(FakeSerial):
            def write(self, data: bytes) -> int:
                result = super().write(data)
                frame = decode_frame(data)
                if frame.command != COMMAND_END:
                    self.data.extend(encode_frame(COMMAND_ACK, frame.sequence, bytes([frame.command])))
                return result

        port = NoEndReplySerial(b"")
        with self.assertRaisesRegex(TimeoutError, "END ACK 未确认"):
            send_firmware(port, prepare_firmware(self.sample_image(), 7),
                          end_timeout_s=0.005)
        self.assertEqual([decode_frame(raw).command for raw in port.writes].count(COMMAND_END), 1)


if __name__ == "__main__":
    unittest.main()
