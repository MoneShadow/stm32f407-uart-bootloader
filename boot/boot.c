#include <stddef.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"

#include "boot.h"
#include "boot_image.h"
#include "boot_jump.h"
#include "boot_protocol.h"
#include "boot_uart_frame.h"

#define BOOT_STARTUP_HELLO_TIMEOUT_MS 1000U

typedef enum {
    BOOT_HELLO_WAIT_STATUS_RECEIVED = 0,
    BOOT_HELLO_WAIT_STATUS_TIMEOUT,
    BOOT_HELLO_WAIT_STATUS_TRANSPORT_ERROR
} boot_hello_wait_status_t;

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
        /* 接收到HELLO之后开始解码 */
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

/*
 *  该函数实现bootloader的主要功能
 *  检查APP是否有效
 *  等待HELLO
 *  等待升级
 *  跳转app
 */
noreturn void boot_run(void) {
    /* 检查app向量表 */
    boot_image_vector_table_t app_vector_table;
    const boot_image_status_t app_status = boot_image_check_vector_table(&app_vector_table);
    /* 有效的逻辑 等待HELLO 超时 跳转app */
    if (app_status == BOOT_IMAGE_STATUS_VALID) {
        const boot_hello_wait_status_t hello_status = boot_wait_for_hello(BOOT_STARTUP_HELLO_TIMEOUT_MS);
        if (hello_status != BOOT_HELLO_WAIT_STATUS_RECEIVED) {
            boot_jump_to_application(&app_vector_table);
        }
    }
    /* 无效的逻辑 等待HELLO(一直等待 因为此时app向量表中的数据是不正确的 根本就没法跳转app层) */
    else {
        while (boot_wait_for_hello(BOOT_STARTUP_HELLO_TIMEOUT_MS) != BOOT_HELLO_WAIT_STATUS_RECEIVED);
    }
    /*
     * 已收到有效 HELLO。
     * 下一阶段会在这里进入升级会话。
     */
    while (1) {
    }
}