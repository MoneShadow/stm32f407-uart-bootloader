#ifndef BOOT_PROTOCOL_H
#define BOOT_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define BOOT_PROTOCOL_SOF0                       0xA5U
#define BOOT_PROTOCOL_SOF1                       0x5AU
#define BOOT_PROTOCOL_VERSION                    0x01U

#define BOOT_PROTOCOL_SOF_SIZE                   2U
#define BOOT_PROTOCOL_FIXED_HEADER_SIZE          8U
#define BOOT_PROTOCOL_CRC_INPUT_HEADER_SIZE      6U
#define BOOT_PROTOCOL_CRC_SIZE                   2U
#define BOOT_PROTOCOL_MAX_PAYLOAD_SIZE           256U
#define BOOT_PROTOCOL_MAX_FRAME_SIZE (BOOT_PROTOCOL_FIXED_HEADER_SIZE + BOOT_PROTOCOL_MAX_PAYLOAD_SIZE + BOOT_PROTOCOL_CRC_SIZE)
#define BOOT_PROTOCOL_MIN_FRAME_SIZE (BOOT_PROTOCOL_FIXED_HEADER_SIZE + BOOT_PROTOCOL_CRC_SIZE)

#define BOOT_PROTOCOL_START_PAYLOAD_SIZE         12U
#define BOOT_PROTOCOL_ACK_PAYLOAD_SIZE           1U
#define BOOT_PROTOCOL_NACK_PAYLOAD_SIZE          2U

#define BOOT_PROTOCOL_LENGTH_OFFSET              6U
#define BOOT_PROTOCOL_ACK_COMMAND_OFFSET         0U
#define BOOT_PROTOCOL_NACK_COMMAND_OFFSET        0U
#define BOOT_PROTOCOL_NACK_REASON_OFFSET         1U

typedef enum {
    BOOT_PROTOCOL_COMMAND_HELLO = 0x01U,
    BOOT_PROTOCOL_COMMAND_START = 0x02U,
    BOOT_PROTOCOL_COMMAND_DATA  = 0x03U,
    BOOT_PROTOCOL_COMMAND_END   = 0x04U,
    BOOT_PROTOCOL_COMMAND_ACK   = 0x80U,
    BOOT_PROTOCOL_COMMAND_NACK  = 0x81U
} boot_protocol_command_t;

typedef enum {
    BOOT_PROTOCOL_NACK_UNSUPPORTED_VERSION  = 0x01U,
    BOOT_PROTOCOL_NACK_UNKNOWN_COMMAND      = 0x02U,
    BOOT_PROTOCOL_NACK_INVALID_LENGTH       = 0x03U,
    BOOT_PROTOCOL_NACK_CRC_MISMATCH         = 0x04U,
    BOOT_PROTOCOL_NACK_UNEXPECTED_SEQUENCE  = 0x05U,
    BOOT_PROTOCOL_NACK_INVALID_STATE        = 0x06U,
    BOOT_PROTOCOL_NACK_FLASH_ERROR          = 0x07U,
    BOOT_PROTOCOL_NACK_IMAGE_ERROR          = 0x08U,
    BOOT_PROTOCOL_NACK_INTERNAL_ERROR       = 0x7FU
} boot_protocol_nack_reason_t;

typedef enum {
    BOOT_PROTOCOL_STATUS_OK = 0,
    BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT,
    BOOT_PROTOCOL_STATUS_UNSUPPORTED_VERSION,
    BOOT_PROTOCOL_STATUS_UNKNOWN_COMMAND,
    BOOT_PROTOCOL_STATUS_INVALID_LENGTH,
    BOOT_PROTOCOL_STATUS_BUFFER_TOO_SMALL,
    BOOT_PROTOCOL_STATUS_INVALID_SOF,
    BOOT_PROTOCOL_STATUS_CRC_MISMATCH
} boot_protocol_status_t;

/*
 * 这是解析后的内部表示，不是线上字节布局。
 * 禁止把这个结构体强制转换成 uint8_t 数组直接收发。
 */
typedef struct {
    uint8_t version;
    uint8_t command;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[BOOT_PROTOCOL_MAX_PAYLOAD_SIZE];
} boot_protocol_frame_t;

boot_protocol_status_t boot_protocol_encode_frame(const boot_protocol_frame_t *frame, uint8_t *encoded, size_t encoded_capacity, size_t *encoded_length);
boot_protocol_status_t boot_protocol_decode_frame(const uint8_t *encoded, size_t encoded_length, boot_protocol_frame_t *frame);

#endif