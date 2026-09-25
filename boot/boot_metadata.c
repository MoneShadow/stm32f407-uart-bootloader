#include "boot_metadata.h"
#include "boot_crc.h"

/* 读取小端序函数 */
static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8U)
         | ((uint32_t)p[2] << 16U)
         | ((uint32_t)p[3] << 24U);
}

/* 写入小端序函数 */
static void write_le32(uint8_t *p, uint32_t value) {
    p[0] = value & 0xFF;
    p[1] = (value >> 8)  & 0xFF;
    p[2] = (value >> 16) & 0xFF;
    p[3] = (value >> 24) & 0xFF;
}

/* 解码 校验 写入 数据 */
boot_metadata_status_t boot_metadata_validate_and_decode(const uint8_t *record, size_t record_length, size_t app_capacity, boot_metadata_info_t *info) {
    /* 检查传入参数是否合法 */
    if (record == NULL || info == NULL || record_length != BOOT_METADATA_RECORD_SIZE) {
        return BOOT_METADATA_STATUS_INVALID_ARGUMENT;
    }
    /* 先解码校验commit_marker */
    const uint32_t commit_marker = read_le32(&record[BOOT_METADATA_COMMIT_MARKER_OFFSET]);
    if (commit_marker != BOOT_METADATA_COMMIT_MARKER) {
        return BOOT_METADATA_STATUS_INVALID_COMMIT_MARKER;
    }
    /* 检查magic */
    const uint32_t magic = read_le32(&record[BOOT_METADATA_MAGIC_OFFSET]);
    if (magic != BOOT_METADATA_MAGIC) {
        return BOOT_METADATA_STATUS_INVALID_MAGIC;
    }
    /* 检查format_version */
    const uint32_t format_version = read_le32(&record[BOOT_METADATA_FORMAT_VERSION_OFFSET]);
    if (format_version != BOOT_METADATA_FORMAT_VERSION) {
        return BOOT_METADATA_STATUS_INVALID_FORMAT_VERSION;
    }
    /* 检查image_size */
    const uint32_t image_size = read_le32(&record[BOOT_METADATA_IMAGE_SIZE_OFFSET]);
    if (image_size < 8 || (size_t)image_size > app_capacity) {
        return BOOT_METADATA_STATUS_INVALID_IMAGE_SIZE;
    }
    /* 计算metadata的crc32 并与实际接收的crc32做比较 */
    const uint32_t crc_calculate = boot_crc32_ieee(record, BOOT_METADATA_METADATA_CRC32_OFFSET);
    const uint32_t crc_reality = read_le32(&record[BOOT_METADATA_METADATA_CRC32_OFFSET]);
    if (crc_calculate != crc_reality) {
        return BOOT_METADATA_STATUS_CRC_MISMATCH;
    }
    /* 开始写入info */
    info->image_size = image_size;
    info->firmware_version = read_le32(&record[BOOT_METADATA_FIRMWARE_VERSION_OFFSET]);
    info->image_crc32 = read_le32(&record[BOOT_METADATA_IMAGE_CRC32_OFFSET]);
    return BOOT_METADATA_STATUS_OK;
}

/* 将info编码到record中 以供写入metadata区 */
boot_metadata_status_t boot_metadata_encode(const boot_metadata_info_t *info, size_t app_capacity, uint8_t *record, size_t record_capacity) {
    /* 检查传入参数 */
    if (info == NULL || record == NULL || record_capacity < BOOT_METADATA_RECORD_SIZE) {
        return BOOT_METADATA_STATUS_INVALID_ARGUMENT;
    }
    /* 检查image_size是否超过app区空间大小 */
    if (info->image_size > app_capacity || info->image_size < 8U) {
        return BOOT_METADATA_STATUS_INVALID_IMAGE_SIZE;
    }
    /* 编码magic formatversion imagesize imagecrc firmwareversion metadatacrc 最后编码commitmarker */
    write_le32(&record[BOOT_METADATA_MAGIC_OFFSET], BOOT_METADATA_MAGIC);
    write_le32(&record[BOOT_METADATA_FORMAT_VERSION_OFFSET], BOOT_METADATA_FORMAT_VERSION);
    write_le32(&record[BOOT_METADATA_IMAGE_SIZE_OFFSET], info->image_size);
    write_le32(&record[BOOT_METADATA_IMAGE_CRC32_OFFSET], info->image_crc32);
    write_le32(&record[BOOT_METADATA_FIRMWARE_VERSION_OFFSET], info->firmware_version);
    /* 计算metadatacrc */
    uint32_t crc = boot_crc32_ieee(record, BOOT_METADATA_METADATA_CRC32_OFFSET);
    write_le32(&record[BOOT_METADATA_METADATA_CRC32_OFFSET], crc);
    /* 最后编码commitmarker */
    write_le32(&record[BOOT_METADATA_COMMIT_MARKER_OFFSET], BOOT_METADATA_COMMIT_MARKER);
    return BOOT_METADATA_STATUS_OK;
}
