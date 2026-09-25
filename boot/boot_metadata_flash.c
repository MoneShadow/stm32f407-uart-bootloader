#include "boot_metadata.h"
#include "boot_memory.h"

/* 从flash中解析metadata 获取info */
boot_metadata_status_t boot_metadata_read_from_flash(boot_metadata_info_t *info) {
    /* 检查传入参数 */
    if (info == NULL) {
        return BOOT_METADATA_STATUS_INVALID_ARGUMENT;
    }
    /* 检查元数据分区 */
    if (BOOT_METADATA_FLASH_END < BOOT_METADATA_FLASH_START || BOOT_METADATA_FLASH_END - BOOT_METADATA_FLASH_START < BOOT_METADATA_RECORD_SIZE) {
        return BOOT_METADATA_STATUS_INVALID_METADATA_SIZE;
    }
    /* APP结束地址不小于起始地址 */
    if (BOOT_APP_FLASH_END <= BOOT_APP_FLASH_START) {
        return BOOT_METADATA_STATUS_INVALID_IMAGE_SIZE;
    }
    /* 从flash中解析metadata 获取info */
    const uint8_t *record = (const uint8_t *)BOOT_METADATA_FLASH_START;
    return boot_metadata_validate_and_decode(record, BOOT_METADATA_RECORD_SIZE, BOOT_APP_FLASH_END - BOOT_APP_FLASH_START, info);
}