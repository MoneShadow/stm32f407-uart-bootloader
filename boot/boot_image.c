#include "boot_image.h"
#include "boot_memory.h"
#include "boot_crc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOT_STACK_ALIGNMENT_MASK   0x07U
#define BOOT_THUMB_BIT              0x01U

/* 检查ResetHandler是否在合理的Flash范围内 */
static bool boot_address_is_in_half_open_range(uintptr_t address, uintptr_t start, uintptr_t end) {
    return (address >= start) && (address < end);
}

/* 检查MSP是合理 */
static bool boot_initial_msp_is_valid(uint32_t initial_msp) {
    /* 检查是否8字节对齐 */
    const bool is_aligned = (initial_msp & BOOT_STACK_ALIGNMENT_MASK) == UINT32_C(0);
    /* 检查MSP地址是否在合理RAM范围 */
    const bool is_in_sram = (initial_msp > BOOT_SRAM_START) && (initial_msp <= BOOT_SRAM_END);
    /* MSP也可以放在CCRAM 这里同样去做一下检查 */
    const bool is_in_ccm_ram = (initial_msp > BOOT_CCM_RAM_START) && (initial_msp <= BOOT_CCM_RAM_END);
    /* 返回检查结果 */
    return is_aligned && (is_in_sram || is_in_ccm_ram);
}

/* APP向量表基础检查并解析保存MSP和ResetHandler */
boot_image_status_t boot_image_check_vector_table(boot_image_vector_table_t *vector_table) {
    /* 检查入参 */
    if (vector_table == NULL) {
        return BOOT_IMAGE_STATUS_INVALID_ARGUMENT;
    }
    /* 建立指向 APP 向量表的指针 */
    const volatile uint32_t *const app_vectors = (const volatile uint32_t *)BOOT_APP_FLASH_START;
    /* 取出msp和resethandler */
    const boot_image_vector_table_t candidate = {.initial_msp = app_vectors[0], .reset_handler = app_vectors[1]};
    /* 检查MSP地址是否合理 */
    if (!boot_initial_msp_is_valid(candidate.initial_msp)) {
        return BOOT_IMAGE_STATUS_INVALID_MSP;   // MSP非法
    }
    /* 检查ResetHandler地址最后一位bit0是否为1 即thumb状态 */
    if ((candidate.reset_handler & BOOT_THUMB_BIT) == UINT32_C(0)) {
        return BOOT_IMAGE_STATUS_INVALID_THUMB_BIT; // thumb状态非法
    }
    /* 清除ResetHandler地址中最后一位bit0 这一位表示的是thumb状态 并不代表实际地址 */
    const uintptr_t reset_handler_address = (uintptr_t)(candidate.reset_handler & ~BOOT_THUMB_BIT);
    /* 检查ResteHandler地址是否合理 */
    if (!boot_address_is_in_half_open_range(reset_handler_address, BOOT_APP_FLASH_START, BOOT_APP_FLASH_END)) {
        return BOOT_IMAGE_STATUS_INVALID_RESET_HANDLER; // ResteHandler地址非法
    }
    /* 数据出参 */
    *vector_table = candidate;
    return BOOT_IMAGE_STATUS_VALID; // 基础检查合法
}

/* 检查app */
boot_image_status_t boot_image_check_image(const boot_metadata_info_t *info) {
    /* 检查传入参数 */
    if (info == NULL) {
        return BOOT_IMAGE_STATUS_INVALID_ARGUMENT;
    }
    /* 检查image_size */
    if (info->image_size < 8 || info->image_size > BOOT_APP_FLASH_END - BOOT_APP_FLASH_START) {
        return BOOT_IMAGE_STATUS_INVALID_IMAGE_SIZE;
    }
    /* 比较CRC */
    const uint8_t *const imageptr = (const uint8_t *)BOOT_APP_FLASH_START;
    uint32_t crc32 = boot_crc32_ieee(imageptr, (size_t)info->image_size);
    if (crc32 != info->image_crc32) {
        return BOOT_IMAGE_STATUS_IMAGE_CRC32_MISMATCH;
    }
    return BOOT_IMAGE_STATUS_VALID;
}

/* 检查安装好的app与metadata中的数据是否一致 */
boot_image_status_t boot_image_check_installed(boot_image_vector_table_t *out) {
    /* 检查传入的参数 */
    if (out == NULL) {
        return BOOT_IMAGE_STATUS_INVALID_ARGUMENT;
    }
    /* 读取metadata区获取info */
    boot_metadata_info_t info;
    if (boot_metadata_read_from_flash(&info) != BOOT_METADATA_STATUS_OK) {
        return BOOT_IMAGE_STATUS_INVALID_METADATA;
    }
    /* 从已安装的app中检查解析获取MSP和RestHandler */
    boot_image_vector_table_t appvector = {0};
    boot_image_status_t state = boot_image_check_vector_table(&appvector);
    if (state != BOOT_IMAGE_STATUS_VALID) {
        return state;
    }
    /* 检查ResetHandler的实际地址确实落在声明的镜像内 */
    uint32_t resetaddress = (uintptr_t)(appvector.reset_handler & ~BOOT_THUMB_BIT);
    if (resetaddress - BOOT_APP_FLASH_START >= info.image_size) {
        return BOOT_IMAGE_STATUS_INVALID_RESET_HANDLER;
    }
    /* 检查app与info中的信息是否匹配 */
    state = boot_image_check_image(&info);
    if (state != BOOT_IMAGE_STATUS_VALID) {
        return state;
    }
    *out = appvector;
    return BOOT_IMAGE_STATUS_VALID;
}