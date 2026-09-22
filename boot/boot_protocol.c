#include <string.h>

#include "boot_crc.h"
#include "boot_protocol.h"

#define BOOT_PROTOCOL_VERSION_OFFSET       2U
#define BOOT_PROTOCOL_COMMAND_OFFSET       3U
#define BOOT_PROTOCOL_SEQUENCE_LOW_OFFSET  4U
#define BOOT_PROTOCOL_SEQUENCE_HIGH_OFFSET 5U
#define BOOT_PROTOCOL_PAYLOAD_OFFSET       8U

static boot_protocol_status_t boot_protocol_validate_fields(uint8_t version, uint8_t command, uint16_t payload_length) {
    if (version != BOOT_PROTOCOL_VERSION) {
        return BOOT_PROTOCOL_STATUS_UNSUPPORTED_VERSION;
    }
    switch (command) {
        case BOOT_PROTOCOL_COMMAND_HELLO:
            if (payload_length != 0) return BOOT_PROTOCOL_STATUS_INVALID_LENGTH;
            break;
        case BOOT_PROTOCOL_COMMAND_START:
            if (payload_length != BOOT_PROTOCOL_START_PAYLOAD_SIZE) return BOOT_PROTOCOL_STATUS_INVALID_LENGTH;
            break;
        case BOOT_PROTOCOL_COMMAND_DATA:
            if (payload_length < 1 || payload_length > BOOT_PROTOCOL_MAX_PAYLOAD_SIZE) return BOOT_PROTOCOL_STATUS_INVALID_LENGTH;
            break;
        case BOOT_PROTOCOL_COMMAND_END:
            if (payload_length != 0) return BOOT_PROTOCOL_STATUS_INVALID_LENGTH;
            break;
        case BOOT_PROTOCOL_COMMAND_ACK:
            if (payload_length != BOOT_PROTOCOL_ACK_PAYLOAD_SIZE) return BOOT_PROTOCOL_STATUS_INVALID_LENGTH;
            break;
        case BOOT_PROTOCOL_COMMAND_NACK:
            if (payload_length != BOOT_PROTOCOL_NACK_PAYLOAD_SIZE) return BOOT_PROTOCOL_STATUS_INVALID_LENGTH;
            break;
        default:
            return BOOT_PROTOCOL_STATUS_UNKNOWN_COMMAND;
    }
    return BOOT_PROTOCOL_STATUS_OK;
}

/* 编码 */
boot_protocol_status_t boot_protocol_encode_frame(const boot_protocol_frame_t *frame, uint8_t *encoded, size_t encoded_capacity, size_t *encoded_length) {
    /* 检查参数是否传入为空 */
    if (frame == NULL || encoded == NULL || encoded_length == NULL) {
        return BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }
    *encoded_length = 0U;
    /* 检查帧数据格式是否正确 */
    boot_protocol_status_t state = boot_protocol_validate_fields(frame->version, frame->command, frame->payload_length);
    if (state != BOOT_PROTOCOL_STATUS_OK) {
        return state;
    }
    /* 计算所需长度并判断提供的buffer是否够大 */
    const size_t required_length = BOOT_PROTOCOL_FIXED_HEADER_SIZE + frame->payload_length + BOOT_PROTOCOL_CRC_SIZE;
    if (required_length > encoded_capacity) {
        return BOOT_PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }
    /* 拼帧 */
    size_t index = 0;
    /* 帧头 */
    encoded[index++] = BOOT_PROTOCOL_SOF0;
    encoded[index++] = BOOT_PROTOCOL_SOF1;
    encoded[index++] = frame->version;
    encoded[index++] = frame->command;
    encoded[index++] = (uint8_t)(frame->sequence & 0xFF);
    encoded[index++] = (uint8_t)(frame->sequence >> 8U);
    encoded[index++] = (uint8_t)(frame->payload_length & 0xFF);
    encoded[index++] = (uint8_t)(frame->payload_length >> 8U);
    /* 数据 */
    for (size_t i = 0; i < frame->payload_length; i++) {
        encoded[index++] = frame->payload[i];
    }
    /* 计算crc并写入 */
    const uint16_t crc = boot_crc16_ccitt_false(&encoded[BOOT_PROTOCOL_SOF_SIZE], BOOT_PROTOCOL_CRC_INPUT_HEADER_SIZE + frame->payload_length);
    encoded[index++] = (uint8_t)(crc & 0xFFU);
    encoded[index++] = (uint8_t)(crc >> 8U);
    /* 返回帧长度 */
    *encoded_length = index;
    return BOOT_PROTOCOL_STATUS_OK;
}

/* 解码 */
boot_protocol_status_t boot_protocol_decode_frame(const uint8_t *encoded, size_t encoded_length, boot_protocol_frame_t *frame) {
    /* 检查入参不为空 */
    if (encoded == NULL || frame == NULL) {
        return BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }
    /* 检查buffer大小合法 */
    if (encoded_length < BOOT_PROTOCOL_MIN_FRAME_SIZE || encoded_length > BOOT_PROTOCOL_MAX_FRAME_SIZE) {
        return BOOT_PROTOCOL_STATUS_INVALID_LENGTH;
    }
    /* 检查帧头 */
    if (encoded[0] != BOOT_PROTOCOL_SOF0 || encoded[1] != BOOT_PROTOCOL_SOF1) {
        return BOOT_PROTOCOL_STATUS_INVALID_SOF;
    }
    /* 检查数据长度是否不超过最大支持长度 */
    const uint16_t payload_length = (uint16_t)encoded[BOOT_PROTOCOL_LENGTH_OFFSET] | ((uint16_t)encoded[BOOT_PROTOCOL_LENGTH_OFFSET + 1U] << 8U);
    if (payload_length > BOOT_PROTOCOL_MAX_PAYLOAD_SIZE) {
        return BOOT_PROTOCOL_STATUS_INVALID_LENGTH;
    }
    /* 检查一个数据包的总长度是否符合预期 */
    const size_t expected_length = BOOT_PROTOCOL_FIXED_HEADER_SIZE + payload_length + BOOT_PROTOCOL_CRC_SIZE;
    if (encoded_length != expected_length) {
        return BOOT_PROTOCOL_STATUS_INVALID_LENGTH;
    }
    /* 计算crc的偏移量 获取crc 计算crc 比较接收的和获取的crc是否一致 */
    const size_t crc_offset = BOOT_PROTOCOL_FIXED_HEADER_SIZE + payload_length;
    const uint16_t received_crc = (uint16_t)encoded[crc_offset] | ((uint16_t)encoded[crc_offset + 1U] << 8U);
    const uint16_t calculated_crc = boot_crc16_ccitt_false(&encoded[BOOT_PROTOCOL_SOF_SIZE], BOOT_PROTOCOL_CRC_INPUT_HEADER_SIZE + payload_length);
    if (received_crc != calculated_crc) {
        return BOOT_PROTOCOL_STATUS_CRC_MISMATCH;
    }
    /* 获取帧头 */
    const uint8_t version = encoded[BOOT_PROTOCOL_VERSION_OFFSET];
    const uint8_t command = encoded[BOOT_PROTOCOL_COMMAND_OFFSET];
    const uint16_t sequence = (uint16_t)encoded[BOOT_PROTOCOL_SEQUENCE_LOW_OFFSET] | ((uint16_t)encoded[BOOT_PROTOCOL_SEQUENCE_HIGH_OFFSET] << 8U);
    /* 检验帧头 */
    boot_protocol_status_t state = boot_protocol_validate_fields(version, command, payload_length);
    if (state != BOOT_PROTOCOL_STATUS_OK) {
        return state;
    }
    /* 写入frame */
    frame->version = version;
    frame->command = command;
    frame->sequence = sequence;
    frame->payload_length = payload_length;
    if (payload_length > 0U) {
        memcpy(frame->payload, &encoded[BOOT_PROTOCOL_PAYLOAD_OFFSET], payload_length);
    }
    return BOOT_PROTOCOL_STATUS_OK;
}