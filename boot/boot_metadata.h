#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include <stdint.h>
#include <stddef.h>


#define BOOT_METADATA_MAGIC_OFFSET               0U
#define BOOT_METADATA_FORMAT_VERSION_OFFSET      4U
#define BOOT_METADATA_IMAGE_SIZE_OFFSET          8U
#define BOOT_METADATA_IMAGE_CRC32_OFFSET        12U
#define BOOT_METADATA_FIRMWARE_VERSION_OFFSET   16U
#define BOOT_METADATA_METADATA_CRC32_OFFSET     20U
#define BOOT_METADATA_COMMIT_MARKER_OFFSET      24U

#define BOOT_METADATA_RECORD_SIZE      28U
#define BOOT_METADATA_MAGIC            0x474D4942U
#define BOOT_METADATA_FORMAT_VERSION   1U
#define BOOT_METADATA_COMMIT_MARKER    0xC0DEC0DEU

typedef enum {
    BOOT_METADATA_STATUS_OK = 0,
    BOOT_METADATA_STATUS_INVALID_ARGUMENT,
    BOOT_METADATA_STATUS_INVALID_COMMIT_MARKER,
    BOOT_METADATA_STATUS_INVALID_MAGIC,
    BOOT_METADATA_STATUS_INVALID_FORMAT_VERSION,
    BOOT_METADATA_STATUS_INVALID_IMAGE_SIZE,
    BOOT_METADATA_STATUS_INVALID_METADATA_SIZE,
    BOOT_METADATA_STATUS_CRC_MISMATCH
} boot_metadata_status_t;

typedef struct {
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t firmware_version;
} boot_metadata_info_t;

boot_metadata_status_t boot_metadata_validate_and_decode(const uint8_t *record, size_t record_length, size_t app_capacity, boot_metadata_info_t *info);
boot_metadata_status_t boot_metadata_read_from_flash(boot_metadata_info_t *info);

#endif