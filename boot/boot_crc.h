#ifndef BOOT_CRC_H
#define BOOT_CRC_H

#include <stddef.h>
#include <stdint.h>

/*
 * data 在 length > 0 时必须有效。
 * length == 0 时返回 CRC 初值 0xFFFF。
 */
uint16_t boot_crc16_ccitt_false(const uint8_t *data, size_t length);

/*
 * 从已有 CRC 状态继续计算，供“协议头 + Payload”分段计算使用。
 * length == 0 时原样返回 crc。
 */
uint16_t boot_crc16_ccitt_false_update(uint16_t crc, const uint8_t *data, size_t length);

/*
 * CRC-32/ISO-HDLC (常称 CRC-32/IEEE)：输入/输出反射，初值与最终异或值
 * 都为 0xFFFFFFFF。与 Python 的 zlib.crc32() 结果一致。
 * 空数据的 CRC 为 0；length > 0 时 data 必须有效。
 */
uint32_t boot_crc32_ieee(const uint8_t *data, size_t length);

/*
 * 从已经完成最终异或的 CRC 值继续计算，初始值传 0。
 * length == 0 时原样返回 crc，可用于分包累计校验。
 */
uint32_t boot_crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t length);

#endif
