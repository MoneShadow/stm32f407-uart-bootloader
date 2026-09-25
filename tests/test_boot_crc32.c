#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "boot_crc.h"

int main(void) {
    static const uint8_t digits[] = "123456789";
    static const uint8_t binary[] = {0x00U, 0x01U, 0x02U, 0x03U, 0xFFU};

    assert(boot_crc32_ieee(NULL, 0U) == 0U);
    assert(boot_crc32_ieee(digits, sizeof(digits) - 1U) == 0xCBF43926U);
    assert(boot_crc32_ieee(binary, sizeof(binary)) == 0x7B35F858U);

    uint32_t crc = boot_crc32_ieee_update(0U, digits, 4U);
    crc = boot_crc32_ieee_update(crc, digits + 4U, 5U);
    assert(crc == 0xCBF43926U);
    assert(boot_crc32_ieee_update(crc, NULL, 0U) == crc);

    puts("boot CRC32 tests passed");
    return 0;
}
