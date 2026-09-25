#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include "boot_metadata.h"

#include <stdint.h>

typedef struct {
    uint32_t initial_msp;
    uint32_t reset_handler;
} boot_image_vector_table_t;

typedef enum {
    BOOT_IMAGE_STATUS_VALID = 0,
    BOOT_IMAGE_STATUS_INVALID_ARGUMENT,
    BOOT_IMAGE_STATUS_INVALID_MSP,
    BOOT_IMAGE_STATUS_INVALID_RESET_HANDLER,
    BOOT_IMAGE_STATUS_INVALID_THUMB_BIT,
    BOOT_IMAGE_STATUS_IMAGE_CRC32_MISMATCH,
    BOOT_IMAGE_STATUS_INVALID_IMAGE_SIZE,
    BOOT_IMAGE_STATUS_INVALID_METADATA
} boot_image_status_t;

boot_image_status_t boot_image_check_vector_table(boot_image_vector_table_t *vector_table);
boot_image_status_t boot_image_check_image(const boot_metadata_info_t *info);
boot_image_status_t boot_image_check_installed(boot_image_vector_table_t *out);

#endif
