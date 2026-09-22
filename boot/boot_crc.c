#include "boot_crc.h"

#define BOOT_CRC16_INITIAL       0xFFFFU
#define BOOT_CRC16_POLYNOMIAL    0x1021U
#define BOOT_CRC16_TOP_BIT       0x8000U

uint16_t boot_crc16_ccitt_false_update(uint16_t crc, const uint8_t *data, size_t length) {
    for (size_t byte_index = 0U; byte_index < length; ++byte_index) {
        crc ^= (uint16_t)((uint16_t)data[byte_index] << 8U);

        for (uint32_t bit_index = 0U; bit_index < 8U; ++bit_index) {
            if ((crc & BOOT_CRC16_TOP_BIT) != 0U) {
                crc = (uint16_t)((crc << 1U) ^ BOOT_CRC16_POLYNOMIAL);
            } else {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }

    return crc;
}

uint16_t boot_crc16_ccitt_false(const uint8_t *data, size_t length) {
    return boot_crc16_ccitt_false_update(BOOT_CRC16_INITIAL, data, length);
}
