#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_metadata.h"

static const size_t app_capacity = 864U * 1024U;

static void assert_record_filled_with(const uint8_t *record, uint8_t value) {
    for (size_t i = 0; i < BOOT_METADATA_RECORD_SIZE; ++i) {
        assert(record[i] == value);
    }
}

static void test_known_record_and_round_trip(void) {
    /* 由已烧录的 6000-byte LED APP 独立计算出的元数据记录。 */
    static const uint8_t expected[BOOT_METADATA_RECORD_SIZE] = {
        0x42U, 0x49U, 0x4DU, 0x47U, 0x01U, 0x00U, 0x00U, 0x00U,
        0x70U, 0x17U, 0x00U, 0x00U, 0x28U, 0x47U, 0x45U, 0x7BU,
        0x01U, 0x00U, 0x00U, 0x00U, 0x8BU, 0x44U, 0xA1U, 0x98U,
        0xDEU, 0xC0U, 0xDEU, 0xC0U
    };
    const boot_metadata_info_t input = {
        .image_size = 6000U,
        .image_crc32 = 0x7B454728U,
        .firmware_version = 1U
    };
    uint8_t record[BOOT_METADATA_RECORD_SIZE] = {0};
    boot_metadata_info_t decoded = {0};

    assert(boot_metadata_encode(&input, app_capacity, record, sizeof(record)) == BOOT_METADATA_STATUS_OK);
    assert(memcmp(record, expected, sizeof(expected)) == 0);
    assert(boot_metadata_validate_and_decode(record, sizeof(record), app_capacity, &decoded) == BOOT_METADATA_STATUS_OK);
    assert(decoded.image_size == input.image_size);
    assert(decoded.image_crc32 == input.image_crc32);
    assert(decoded.firmware_version == input.firmware_version);
}

static void test_other_fields_are_not_hardcoded(void) {
    const boot_metadata_info_t input = {
        .image_size = 8U,
        .image_crc32 = 0x12345678U,
        .firmware_version = 0xDEADBEEFU
    };
    uint8_t record[BOOT_METADATA_RECORD_SIZE] = {0};
    boot_metadata_info_t decoded = {0};

    assert(boot_metadata_encode(&input, app_capacity, record, sizeof(record)) == BOOT_METADATA_STATUS_OK);
    assert(boot_metadata_validate_and_decode(record, sizeof(record), app_capacity, &decoded) == BOOT_METADATA_STATUS_OK);
    assert(decoded.image_size == input.image_size);
    assert(decoded.image_crc32 == input.image_crc32);
    assert(decoded.firmware_version == input.firmware_version);
}

static void test_invalid_input_does_not_change_record(void) {
    boot_metadata_info_t input = {
        .image_size = 7U,
        .image_crc32 = 0x12345678U,
        .firmware_version = 1U
    };
    uint8_t record[BOOT_METADATA_RECORD_SIZE];
    memset(record, 0xA5, sizeof(record));

    assert(boot_metadata_encode(&input, app_capacity, record, sizeof(record)) == BOOT_METADATA_STATUS_INVALID_IMAGE_SIZE);
    assert_record_filled_with(record, 0xA5U);

    input.image_size = (uint32_t)app_capacity + 1U;
    assert(boot_metadata_encode(&input, app_capacity, record, sizeof(record)) == BOOT_METADATA_STATUS_INVALID_IMAGE_SIZE);
    assert_record_filled_with(record, 0xA5U);

    input.image_size = 6000U;
    assert(boot_metadata_encode(&input, app_capacity, record, sizeof(record) - 1U) == BOOT_METADATA_STATUS_INVALID_ARGUMENT);
    assert_record_filled_with(record, 0xA5U);

    assert(boot_metadata_encode(NULL, app_capacity, record, sizeof(record)) == BOOT_METADATA_STATUS_INVALID_ARGUMENT);
    assert_record_filled_with(record, 0xA5U);
}

int main(void) {
    test_known_record_and_round_trip();
    test_other_fields_are_not_hardcoded();
    test_invalid_input_does_not_change_record();

    puts("boot metadata tests passed");
    return 0;
}
