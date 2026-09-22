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

#endif