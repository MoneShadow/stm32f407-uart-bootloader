#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BOOT_FLASH_STATUS_OK = 0,
    BOOT_FLASH_STATUS_INVALID_ARGUMENT,
    BOOT_FLASH_STATUS_NOT_ERASED,
    BOOT_FLASH_STATUS_UNLOCK_FAILED,
    BOOT_FLASH_STATUS_ERASE_FAILED,
    BOOT_FLASH_STATUS_PROGRAM_FAILED,
    BOOT_FLASH_STATUS_VERIFY_FAILED,
    BOOT_FLASH_STATUS_LOCK_FAILED
} boot_flash_status_t;

bool boot_flash_is_range_valid(uintptr_t address, size_t length);
bool boot_flash_get_sector(uintptr_t address, uint32_t *sector);
boot_flash_status_t boot_flash_erase_sector(uintptr_t address);
boot_flash_status_t boot_flash_program_word(uintptr_t address, uint32_t data);
boot_flash_status_t boot_flash_program_metadata_word(uintptr_t address, uint32_t data);
boot_flash_status_t boot_flash_program_metadata_record(const uint8_t *record, size_t length);
boot_flash_status_t boot_flash_erase_metadata_sector(void);
boot_flash_status_t boot_flash_program_bytes(uintptr_t address, const uint8_t *data, size_t length);
boot_flash_status_t boot_flash_erase_app_image(size_t image_size);

#endif