#include <stddef.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"

#include "boot.h"
#include "boot_image.h"
#include "boot_jump.h"
#include "boot_protocol.h"
#include "boot_uart_frame.h"
#include "boot_uart.h"
#include "boot_memory.h"

#define BOOT_STARTUP_HELLO_TIMEOUT_MS 1000U
#define BOOT_WAIT_START_TIMEOUT_MS    5000U
#define BOOT_UART_RESPONSE_TIMEOUT_MS 100U

typedef enum {
    BOOT_HELLO_WAIT_STATUS_RECEIVED = 0,
    BOOT_HELLO_WAIT_STATUS_TIMEOUT,
    BOOT_HELLO_WAIT_STATUS_TRANSPORT_ERROR
} boot_hello_wait_status_t;

typedef enum {
    BOOT_START_WAIT_STATUS_RECEIVED = 0,
    BOOT_START_WAIT_STATUS_TIMEOUT,
    BOOT_START_WAIT_STATUS_TRANSPORT_ERROR
} boot_start_wait_status_t;

typedef enum {
    BOOT_REPLY_STATUS_OK = 0,
    BOOT_REPLY_STATUS_ENCODE_ERROR,
    BOOT_REPLY_STATUS_TRANSPORT_ERROR
} boot_reply_status_t;

static uint8_t g_encoded_frame[BOOT_PROTOCOL_MAX_FRAME_SIZE];
static boot_protocol_frame_t g_decoded_frame;
static size_t encoded_length;

/* 等待HELLO函数 只有超时或错误才会停止 */
static boot_hello_wait_status_t boot_wait_for_hello(uint32_t timeout_ms) {
    uint32_t start_tick = HAL_GetTick();
    while (1) {
        /* 计算已经占用的时间 */
        uint32_t elapsed = HAL_GetTick() - start_tick;
        /* 如果占用时间已经超过总等待时间 就返回超时 不然之后会出现回绕的情况 导致remaining的数值变得超级大 */
        if (elapsed >= timeout_ms) {
            return BOOT_HELLO_WAIT_STATUS_TIMEOUT;
        }
        /* 计算剩余时间 */
        uint32_t remaining = timeout_ms - elapsed;
        /* 用剩余时间继续接收数据流 */
        boot_uart_frame_status_t frame_state = boot_uart_frame_receive(g_encoded_frame, sizeof(g_encoded_frame), &encoded_length, remaining);
        if (frame_state != BOOT_UART_FRAME_STATUS_OK) {
            /* 超时返回 */
            if (frame_state == BOOT_UART_FRAME_STATUS_TIMEOUT) {
                return BOOT_HELLO_WAIT_STATUS_TIMEOUT;
            }
            /* 帧头声明的长度非法 忽略 继续循环 */
            if (frame_state == BOOT_UART_FRAME_STATUS_INVALID_LENGTH) {
                continue;
            }
            /* 其余错误直接返回 到这里了 也就只省下其余错误了 直接返回就好 */
            return BOOT_HELLO_WAIT_STATUS_TRANSPORT_ERROR;
        }
        /* 接收到有效命令之后开始解码 */
        boot_protocol_status_t protocol_state = boot_protocol_decode_frame(g_encoded_frame, encoded_length, &g_decoded_frame);
        /* 解码失败 忽略 继续循环 */
        if (protocol_state != BOOT_PROTOCOL_STATUS_OK) {
            continue;
        }
        /* 接收到HELLO 返回接收成功 */
        if (g_decoded_frame.command == BOOT_PROTOCOL_COMMAND_HELLO && g_decoded_frame.sequence == 0x00) {
            return BOOT_HELLO_WAIT_STATUS_RECEIVED;
        }else { // 其余合法命令 重复循环
            continue;
        }
    }
}

/* 发送ACK函数 内部调用：填写原解码数据 编码 发送 */
static boot_reply_status_t boot_send_ack(uint8_t acknowledged_command, uint16_t sequence) {
    size_t ack_encoded_length = 0U;
    /* 填写ACK解码数据 */
    boot_protocol_frame_t ack_frame = {.version = BOOT_PROTOCOL_VERSION, .command = BOOT_PROTOCOL_COMMAND_ACK, .sequence = sequence, .payload_length = BOOT_PROTOCOL_ACK_PAYLOAD_SIZE};
    ack_frame.payload[BOOT_PROTOCOL_ACK_COMMAND_OFFSET] = acknowledged_command;
    /* 重新编码ACK 如果编码失败就认为应答失败 */
    if (boot_protocol_encode_frame(&ack_frame, g_encoded_frame, sizeof(g_encoded_frame), &ack_encoded_length) != BOOT_PROTOCOL_STATUS_OK) {
        return BOOT_REPLY_STATUS_ENCODE_ERROR;
    }
    /* 发送ACK 超时等待100ms */
    if (boot_uart_transmit(g_encoded_frame, ack_encoded_length, BOOT_UART_RESPONSE_TIMEOUT_MS) != BOOT_UART_STATUS_OK) {
        return BOOT_REPLY_STATUS_TRANSPORT_ERROR;
    }
    return BOOT_REPLY_STATUS_OK;
}

/* 发送NACK */
static boot_reply_status_t boot_send_nack(uint8_t rejected_command, uint16_t sequence, boot_protocol_nack_reason_t reason) {
    size_t nack_encoded_length = 0U;
    /* 填写NACK解码数据 */
    boot_protocol_frame_t nack_frame = {.version = BOOT_PROTOCOL_VERSION, .command = BOOT_PROTOCOL_COMMAND_NACK, .sequence = sequence, .payload_length = BOOT_PROTOCOL_NACK_PAYLOAD_SIZE};
    nack_frame.payload[BOOT_PROTOCOL_NACK_COMMAND_OFFSET] = rejected_command;
    nack_frame.payload[BOOT_PROTOCOL_NACK_REASON_OFFSET] = reason;
    /* 重新编码NACK 如果编码失败就认为应答失败 */
    if (boot_protocol_encode_frame(&nack_frame, g_encoded_frame, sizeof(g_encoded_frame), &nack_encoded_length) != BOOT_PROTOCOL_STATUS_OK) {
        return BOOT_REPLY_STATUS_ENCODE_ERROR;
    }
    /* 发送NACK 超时等待100ms */
    if (boot_uart_transmit(g_encoded_frame, nack_encoded_length, BOOT_UART_RESPONSE_TIMEOUT_MS) != BOOT_UART_STATUS_OK) {
        return BOOT_REPLY_STATUS_TRANSPORT_ERROR;
    }
    return BOOT_REPLY_STATUS_OK;
}

/* 等待Start命令 */
static boot_start_wait_status_t boot_wait_for_start(uint32_t timeout_ms, uint32_t *image_size, uint32_t *image_crc32, uint32_t *firmware_version) {
    /* 检查参数 */
    if (image_size == NULL || image_crc32 == NULL || firmware_version == NULL) {
        return BOOT_START_WAIT_STATUS_TRANSPORT_ERROR;
    }
    uint32_t start_tick = HAL_GetTick();
    while (1) {
        /* 超时逻辑(严格窗口时间) */
        uint32_t elapsed = HAL_GetTick() - start_tick;
        if (elapsed >= timeout_ms) {
            return BOOT_START_WAIT_STATUS_TIMEOUT;
        }
        /* 用剩余时间去接收信息流 */
        uint32_t remaining = timeout_ms - elapsed;
        boot_uart_frame_status_t frame_state = boot_uart_frame_receive(g_encoded_frame, sizeof(g_encoded_frame), &encoded_length, remaining);
        if (frame_state != BOOT_UART_FRAME_STATUS_OK) {
            /* 超时返回 */
            if (frame_state == BOOT_UART_FRAME_STATUS_TIMEOUT) {
                return BOOT_START_WAIT_STATUS_TIMEOUT;
            }
            /* 帧头声明的长度非法 忽略 继续循环 */
            if (frame_state == BOOT_UART_FRAME_STATUS_INVALID_LENGTH) {
                continue;
            }
            /* 其余错误直接返回 到这里了 也就只省下其余错误了 直接返回就好 */
            return BOOT_START_WAIT_STATUS_TRANSPORT_ERROR;
        }
        /* 接收到有效命令之后开始解码 */
        boot_protocol_status_t protocol_state = boot_protocol_decode_frame(g_encoded_frame, encoded_length, &g_decoded_frame);
        /* 解码失败 忽略 继续循环 */
        if (protocol_state != BOOT_PROTOCOL_STATUS_OK) {
            continue;
        }
        /* 接收到Start 判断该Start帧是否合法有效 */
        if (g_decoded_frame.command == BOOT_PROTOCOL_COMMAND_START) {
            /* 命令与序号不符 发送NACK并记录状态 重复 */
            if (g_decoded_frame.sequence != 0x01) {
                if (boot_send_nack(g_decoded_frame.command, g_decoded_frame.sequence, BOOT_PROTOCOL_NACK_UNEXPECTED_SEQUENCE) != BOOT_REPLY_STATUS_OK) {
                    return BOOT_START_WAIT_STATUS_TRANSPORT_ERROR;
                }
                continue;
            }
            /* 获取payload Start帧的payload包含 总的固件大小 固件CRC32 和 固件版本 */
            uint32_t image_size0 = (uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_IMAGE_SIZE_OFFSET] |
                                  ((uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_IMAGE_SIZE_OFFSET + 1U] << 8) |
                                  ((uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_IMAGE_SIZE_OFFSET + 2U] << 16) |
                                  ((uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_IMAGE_SIZE_OFFSET + 3U] << 24);

            uint32_t image_crc320 = (uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_CRC_32_OFFSET] |
                                   ((uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_CRC_32_OFFSET + 1U] << 8) |
                                   ((uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_CRC_32_OFFSET + 2U] << 16) |
                                   ((uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_CRC_32_OFFSET + 3U] << 24);

            uint32_t firmware_version0 = (uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_FIRMWARE_VERSION_OFFSET] |
                                        ((uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_FIRMWARE_VERSION_OFFSET + 1U] << 8) |
                                        ((uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_FIRMWARE_VERSION_OFFSET + 2U] << 16) |
                                        ((uint32_t)g_decoded_frame.payload[BOOT_PROTOCOL_START_FIRMWARE_VERSION_OFFSET + 3U] << 24);
            /* 检查Start帧的image_size是否合法 */
            if (image_size0 < 8 || image_size0 > BOOT_APP_FLASH_END - BOOT_APP_FLASH_START) {
                if (boot_send_nack(g_decoded_frame.command, g_decoded_frame.sequence, BOOT_PROTOCOL_NACK_INVALID_LENGTH) != BOOT_REPLY_STATUS_OK) {
                    return BOOT_START_WAIT_STATUS_TRANSPORT_ERROR;
                }
                continue;
            }
            /* 出参 */
            *image_size = image_size0;
            *image_crc32 = image_crc320;
            *firmware_version = firmware_version0;
        }
        /* 如果重复接收到HELLO 则重新发送对应ACK 并记录发送状态 */
        else if (g_decoded_frame.command == BOOT_PROTOCOL_COMMAND_HELLO) {
            /* 命令与序号不符 */
            if (g_decoded_frame.sequence != 0x00) {
                if (boot_send_nack(g_decoded_frame.command, g_decoded_frame.sequence, BOOT_PROTOCOL_NACK_UNEXPECTED_SEQUENCE) != BOOT_REPLY_STATUS_OK) {
                    return BOOT_START_WAIT_STATUS_TRANSPORT_ERROR;
                }
                continue;
            }
            if (boot_send_ack(g_decoded_frame.command, g_decoded_frame.sequence) != BOOT_REPLY_STATUS_OK) {
                return BOOT_START_WAIT_STATUS_TRANSPORT_ERROR;
            }
            continue;
        }
        else { // 其余合法命令 重复循环
            if (boot_send_nack(g_decoded_frame.command, g_decoded_frame.sequence, BOOT_PROTOCOL_NACK_INVALID_STATE) != BOOT_REPLY_STATUS_OK) {
                return BOOT_START_WAIT_STATUS_TRANSPORT_ERROR;
            }
            continue;
        }
        return BOOT_START_WAIT_STATUS_RECEIVED;
    }
}

/*
 *  该函数实现bootloader的主要功能
 *  检查APP是否有效
 *  等待HELLO
 *  等待升级
 *  跳转app
 */
noreturn void boot_run(void) {
    /* 检查app向量表以及完整的app固件 */
    boot_image_vector_table_t app_vector_table;
    const boot_image_status_t app_status = boot_image_check_installed(&app_vector_table);
    /* 有效的逻辑 等待HELLO 超时 跳转app */
    if (app_status == BOOT_IMAGE_STATUS_VALID) {
        /* 等待HELLO 等待超时就跳转APP */
        if (boot_wait_for_hello(BOOT_STARTUP_HELLO_TIMEOUT_MS) != BOOT_HELLO_WAIT_STATUS_RECEIVED) {
            boot_jump_to_application(&app_vector_table);
        }
        /* 发送ACK 发送失败就跳转APP */
        if (boot_send_ack(g_decoded_frame.command, g_decoded_frame.sequence) != BOOT_REPLY_STATUS_OK) {
            boot_jump_to_application(&app_vector_table);
        }
    }
    /* 无效的逻辑 等待HELLO(一直等待 因为此时app向量表中的数据是不正确的 根本就没法跳转app层) */
    else {
        while (1) {
            /* 等待HELLO 直到收到HELLO命令 */
            if (boot_wait_for_hello(BOOT_STARTUP_HELLO_TIMEOUT_MS) != BOOT_HELLO_WAIT_STATUS_RECEIVED) {
                continue;
            }
            /* 每收到一次HELLO 只尝试发送一次ACK 失败后重新等待新的HELLO */
            if (boot_send_ack(g_decoded_frame.command, g_decoded_frame.sequence) == BOOT_REPLY_STATUS_OK) {
                break;
            }
        }
    }
    /*
     * 已收到有效 HELLO。
     * 下一阶段会在这里进入升级会话。
     */
    uint32_t image_size, image_crc32, firmware_version;
    while (1) {
        /* 判断是否等到Start帧 */
        if (boot_wait_for_start(BOOT_WAIT_START_TIMEOUT_MS, &image_size, &image_crc32, &firmware_version) != BOOT_START_WAIT_STATUS_RECEIVED) {
            /* 超时且app有效 跳转app */
            if (app_status == BOOT_IMAGE_STATUS_VALID) {
                boot_jump_to_application(&app_vector_table);
            }
            /* 无效 接着等待HELLO */
            else {
                while (1) {
                    /* 等待HELLO 直到收到HELLO命令 */
                    if (boot_wait_for_hello(BOOT_STARTUP_HELLO_TIMEOUT_MS) != BOOT_HELLO_WAIT_STATUS_RECEIVED) {
                        continue;
                    }
                    /* 每收到一次HELLO 只尝试发送一次ACK 失败后重新等待新的HELLO */
                    if (boot_send_ack(g_decoded_frame.command, g_decoded_frame.sequence) == BOOT_REPLY_STATUS_OK) {
                        break;
                    }
                }
            }
        }
        else {
            while (1) {
            }
        }
    }
}