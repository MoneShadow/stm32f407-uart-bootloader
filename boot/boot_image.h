#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include <stdint.h>

typedef struct {
    uint32_t initial_msp;
    uint32_t reset_handler;
} boot_image_vector_table_t;

typedef enum {
    BOOT_IMAGE_STATUS_VALID = 0,
    BOOT_IMAGE_STATUS_INVALID_MSP,
    BOOT_IMAGE_STATUS_INVALID_RESET_HANDLER,
    BOOT_IMAGE_STATUS_INVALID_THUMB_BIT
} boot_image_status_t;

boot_image_status_t boot_image_check_vector_table(boot_image_vector_table_t *vector_table);

#endif
