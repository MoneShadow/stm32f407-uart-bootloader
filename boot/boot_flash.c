#include "stm32f4xx_hal.h"
#include "boot_flash.h"
#include "boot_memory.h"
#include "boot_metadata.h"

typedef struct {
    uintptr_t end_address;
    uint32_t sector;
} boot_flash_sector_info_t;

/* 每个 end_address 都是不包含在当前 Sector 内的结束地址 表中只包含 APP 可以使用的 Sector 2–10， Sector 11 给meta使用了 */
static const boot_flash_sector_info_t g_app_flash_sectors[] = {
    {0x0800C000U, FLASH_SECTOR_2},
    {0x08010000U, FLASH_SECTOR_3},
    {0x08020000U, FLASH_SECTOR_4},
    {0x08040000U, FLASH_SECTOR_5},
    {0x08060000U, FLASH_SECTOR_6},
    {0x08080000U, FLASH_SECTOR_7},
    {0x080A0000U, FLASH_SECTOR_8},
    {0x080C0000U, FLASH_SECTOR_9},
    {0x080E0000U, FLASH_SECTOR_10},
};

/* 已确认的 APP Flash 范围：[0x08008000, 0x080E0000) 前闭后开 */
bool boot_flash_is_range_valid(uintptr_t address, size_t length) {
    if (length == 0U) {
        return false;
    }
    /* 传入地址小于APP起始地址 or 大于等于APP结束地址 */
    if (address < BOOT_APP_FLASH_START || address >= BOOT_APP_FLASH_END) {
        return false;
    }
    /* 这里判断传入的地址加上长度是否小于APP开始到结束说占用的空间大小 如果过大的话是装不进去的 */
    /* 程序来到这里就已经确定了address是小于BOOT_APP_FLASH_END 所以这里的减法是不会发生回绕问题的 相比较而言 加法就更容易发生回绕问题导致判断失误 */
    return (length <= (BOOT_APP_FLASH_END - address));
}

/* address -> sector */
bool boot_flash_get_sector(uintptr_t address, uint32_t *sector) {
    /* 检查传入参数是否有效 */
    if (sector == NULL || !boot_flash_is_range_valid(address, 1U)) {
        return false;
    }
    /* 计算出APP总共可以占用的sector个数 */
    const size_t sector_count = sizeof(g_app_flash_sectors) / sizeof(g_app_flash_sectors[0]);
    /* 循环遍历 找到传入的address属于哪个sector */
    for (size_t index = 0U; index < sector_count; ++index) {
        if (address < g_app_flash_sectors[index].end_address) {
            *sector = g_app_flash_sectors[index].sector;
            return true;
        }
    }
    return false;
}

/* 擦除传出的地址所属的sector */
boot_flash_status_t boot_flash_erase_sector(uintptr_t address) {
    /* 传输地址转换 */
    uint32_t sector;
    if (!boot_flash_get_sector(address, &sector)) {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    /* 配置FLASH结构体 */
    FLASH_EraseInitTypeDef erase_init = {0};
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;     // 擦除范围 sector
    erase_init.Banks = FLASH_BANK_1;                    // 擦除的BANK1 这里没有作用 只有指定BANK擦除的时候此处配置才会生效
    erase_init.Sector = sector;                         // 要擦除的sector
    erase_init.NbSectors = 1U;                          // 要擦除的sector个数 从指定sector开始到依次递增
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;    // FLASH工作的电压范围
    /* 解锁FLASH */
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return BOOT_FLASH_STATUS_UNLOCK_FAILED;
    }
    /* 0xFFFFFFFFU 表示没有发生擦除错误的 Sector。 */
    uint32_t sector_error = 0xFFFFFFFFU;
    const HAL_StatusTypeDef erase_status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    /* 不管前面的操作结果 只要解锁FLASH就必须重新上锁FLASH */
    const HAL_StatusTypeDef lock_status = HAL_FLASH_Lock();
    /* 判断是否擦除成功 */
    if (erase_status != HAL_OK || sector_error != 0xFFFFFFFFU) {
        return BOOT_FLASH_STATUS_ERASE_FAILED;
    }
    /* 判断是否成功上锁 */
    if (lock_status != HAL_OK) {
        return BOOT_FLASH_STATUS_LOCK_FAILED;
    }
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_flash_program_word(uintptr_t address, uint32_t data) {
    /* 检查地址是否合法 长度是否合法 是否4字节对齐 */
    if (!boot_flash_is_range_valid(address, sizeof(data)) || (address & (sizeof(uint32_t) - 1U)) != 0U) {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    /* 指针不能改 指针所指向的内容也不能改 */
    const volatile uint32_t *const flash_word = (const volatile uint32_t *)address;
    /* 先擦除后写入 */
    if (*flash_word != 0xFFFFFFFFU) {
        return BOOT_FLASH_STATUS_NOT_ERASED;
    }
    /* 解锁 */
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return BOOT_FLASH_STATUS_UNLOCK_FAILED;
    }
    /* 写入数据 */
    const HAL_StatusTypeDef program_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, (uint32_t)address, (uint64_t)data);
    /* 成功解锁后，无论编程结果如何，都必须重新锁定 */
    const HAL_StatusTypeDef lock_status = HAL_FLASH_Lock();
    /* 检查写入状态 */
    if (program_status != HAL_OK) {
        return BOOT_FLASH_STATUS_PROGRAM_FAILED;
    }
    /* 检查上锁状态 */
    if (lock_status != HAL_OK) {
        return BOOT_FLASH_STATUS_LOCK_FAILED;
    }
    /*
     * 清理 Flash 指令/数据缓存，确保回读和随后执行 APP 时
     * 不会使用编程前缓存的旧内容。
     */
    FLASH_FlushCaches();
    /* 检查数据是否被正确写入 */
    if (*flash_word != data) {
        return BOOT_FLASH_STATUS_VERIFY_FAILED;
    }
    return BOOT_FLASH_STATUS_OK;
}

/* 只写记录中的一个 Word */
boot_flash_status_t boot_flash_program_metadata_word(uintptr_t address, uint32_t data) {
    /* 检查地址是否合法 是否4字节对齐 */
    if (BOOT_METADATA_FLASH_END < BOOT_METADATA_FLASH_START ||
        BOOT_METADATA_FLASH_END - BOOT_METADATA_FLASH_START < BOOT_METADATA_RECORD_SIZE ||
        address < BOOT_METADATA_FLASH_START ||
        address - BOOT_METADATA_FLASH_START > BOOT_METADATA_RECORD_SIZE - sizeof(uint32_t) ||
        (address & (sizeof(uint32_t) - 1U)) != 0U) {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    /* 指针不能改 指针所指向的内容也不能改 */
    const volatile uint32_t *const metadata_word = (const volatile uint32_t *)address;
    /* 先擦除后写入 */
    if (*metadata_word != 0xFFFFFFFFU) {
        return BOOT_FLASH_STATUS_NOT_ERASED;
    }
    /* 解锁 */
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return BOOT_FLASH_STATUS_UNLOCK_FAILED;
    }
    /* 写入数据 */
    const HAL_StatusTypeDef program_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, (uint32_t)address, (uint64_t)data);
    /* 成功解锁后，无论编程结果如何，都必须重新锁定 */
    const HAL_StatusTypeDef lock_status = HAL_FLASH_Lock();
    /* 检查写入状态 */
    if (program_status != HAL_OK) {
        return BOOT_FLASH_STATUS_PROGRAM_FAILED;
    }
    /* 检查上锁状态 */
    if (lock_status != HAL_OK) {
        return BOOT_FLASH_STATUS_LOCK_FAILED;
    }
    /*
     * 清理 Flash 指令/数据缓存，确保回读和随后执行 APP 时
     * 不会使用编程前缓存的旧内容。
     */
    FLASH_FlushCaches();
    /* 检查数据是否被正确写入 */
    if (*metadata_word != data) {
        return BOOT_FLASH_STATUS_VERIFY_FAILED;
    }
    return BOOT_FLASH_STATUS_OK;
}

/* 写入metadata */
boot_flash_status_t boot_flash_program_metadata_record(const uint8_t *record, size_t length) {
    /* 检查传入参数 */
    if (record == NULL || length != BOOT_METADATA_RECORD_SIZE) {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    /* 检查要写入的7个word是否已经被擦除 */
    const uint32_t *ptr = (const uint32_t *)BOOT_METADATA_FLASH_START;
    for (uint8_t i = 0; i < 7; i++) {
        if (ptr[i] != 0xFFFFFFFF) {
            return BOOT_FLASH_STATUS_NOT_ERASED;
        }
    }
    /* 小端序取出record 依次写入metadata */
    for (uint8_t i = 0; i < 7; i++) {
        uint32_t word = ((uint32_t)record[i * 4])
                      | ((uint32_t)record[i * 4 + 1] << 8)
                      | ((uint32_t)record[i * 4 + 2] << 16)
                      | ((uint32_t)record[i * 4 + 3] << 24);
        boot_flash_status_t state = boot_flash_program_metadata_word((uintptr_t)&ptr[i], word);
        if (state != BOOT_FLASH_STATUS_OK) {
            return state;
        }
    }
    return BOOT_FLASH_STATUS_OK;
}