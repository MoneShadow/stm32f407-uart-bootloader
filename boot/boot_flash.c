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

/* 向 APP Flash 写入一个 32 位 Word，并立即回读校验 address 必须按 4 字节对齐，并且目标 Word 必须处于擦除态 */
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

/* 擦除metadata区 */
boot_flash_status_t boot_flash_erase_metadata_sector(void) {
    /* 配置FLASH结构体 */
    FLASH_EraseInitTypeDef erase_init = {0};
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;     // 擦除范围 sector
    erase_init.Banks = FLASH_BANK_1;                    // 擦除的BANK1 这里没有作用 只有指定BANK擦除的时候此处配置才会生效
    erase_init.Sector = FLASH_SECTOR_11;                // 要擦除的sector
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

/* 将接收解析来的数据拆成word */
boot_flash_status_t boot_flash_program_bytes(uintptr_t address, const uint8_t *data, size_t length) {
    /* 入参检查 */
    if (data == NULL) {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    /* 检查地址是否四字节对齐 检查长度是否不为0 */
    if (address % sizeof(uint32_t) != 0 || length == 0) {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    /* 检查地址加上数据长度(含补齐字节)后是否还在app分区 */
    /* 验证原始长度 */
    if (!boot_flash_is_range_valid(address, length)) {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    const size_t word_size = sizeof(uint32_t);
    const size_t remainder = length % word_size;
    const size_t padding = remainder ? word_size - remainder : 0U;
    const size_t programmed_length = length + padding;
    /* 验证补齐后的长度 */
    if (!boot_flash_is_range_valid(address, programmed_length)) {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    /* 逐word写入 */
    for (size_t offset = 0U; offset < programmed_length; offset += word_size) {
        uint32_t value_word = 0U;
        /* 每个word逐字节读取 */
        for (size_t i = 0U; i < word_size; i++) {
            const size_t position = offset + i;
            /* 发现空字节(取字节位置超过实际传入的长度)就用0xFF替代 */
            const uint8_t value_byte = position < length ? data[position] : 0xFFU;
            value_word |= (uint32_t)value_byte << (8U * i);
        }
        /* 写入 */
        boot_flash_status_t status = boot_flash_program_word(address + offset, value_word);
        if (status != BOOT_FLASH_STATUS_OK) {
            return status;
        }
    }
    /* 完工 */
    return BOOT_FLASH_STATUS_OK;
}

/* 根据固件大小擦除指定的sector */
boot_flash_status_t boot_flash_erase_app_image(size_t image_size) {
    /* 检查app起始地址加上固件大小是否在app分区的合法范围内 */
    if (!boot_flash_is_range_valid(BOOT_APP_FLASH_START, image_size)) {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    /* 计算镜像的结束地址 */
    uintptr_t image_end = BOOT_APP_FLASH_START + image_size;
    /* 擦除小于image_end的sector */
    /* 无论app固件多小，sector2都是一定会被擦除的 所以这里使用先擦除后判断的逻辑 */
    uint8_t i = 0;
    do {
        boot_flash_status_t state = boot_flash_erase_sector(g_app_flash_sectors[i].end_address - 4U);
        if (state != BOOT_FLASH_STATUS_OK) {
            return state;
        }
        i++;
    } while (g_app_flash_sectors[i - 1U].end_address < image_end);
    return BOOT_FLASH_STATUS_OK;
}

/* 接收start帧之后 发送startACK前 开始擦除元数据和App固件 */
boot_flash_status_t boot_flash_prepare_app_image(size_t image_size) {
    /* 检查固件长度是否合法(小于 8 字节或超出 APP 分区的长度均为非法) */
    if (image_size < 8U || !boot_flash_is_range_valid(BOOT_APP_FLASH_START, image_size)) {
        return BOOT_FLASH_STATUS_INVALID_ARGUMENT;
    }
    /* 开始擦除metadata */
    boot_flash_status_t state = boot_flash_erase_metadata_sector();
    if (state != BOOT_FLASH_STATUS_OK) {
        return state;
    }
    /* 开始擦除app固件 */
    state = boot_flash_erase_app_image(image_size);
    if (state != BOOT_FLASH_STATUS_OK) {
        return state;
    }
    return BOOT_FLASH_STATUS_OK;
}
