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

/* 检查一个非空地址范围是否完整位于 APP Flash end = address + length，且 end 不包含在范围内 */
bool boot_flash_is_range_valid(uintptr_t address, size_t length);
/* 将 APP Flash 内的地址转换为 HAL Sector 编号 成功时写入 sector 失败时不修改 sector */
bool boot_flash_get_sector(uintptr_t address, uint32_t *sector);
/* 擦除 address 所在的整个 APP Sector address 必须位于 APP Flash，Sector 0–1 永远不会被擦除 */
boot_flash_status_t boot_flash_erase_sector(uintptr_t address);
/* 向 APP Flash 写入一个 32 位 Word，并立即回读校验 address 必须按 4 字节对齐，并且目标 Word 必须处于擦除态 */
boot_flash_status_t boot_flash_program_word(uintptr_t address, uint32_t data);
boot_flash_status_t boot_flash_program_metadata_word(uintptr_t address, uint32_t data);
boot_flash_status_t boot_flash_program_metadata_record(const uint8_t *record, size_t length);
boot_flash_status_t boot_flash_erase_metadata_sector(void);
boot_flash_status_t boot_flash_program_bytes(uintptr_t address, const uint8_t *data, size_t length);

#endif