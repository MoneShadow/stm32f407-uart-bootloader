#include "stm32f4xx_hal.h"

#include "boot_protocol.h"
#include "boot_uart.h"
#include "boot_uart_frame.h"

/* 接收函数 */
static boot_uart_frame_status_t boot_uart_frame_receive_exact(uint8_t *data, size_t length, uint32_t start_tick, uint32_t timeout_ms) {
    /* 查看传入参数是否为空 */
    if (data == NULL || length == 0U) {
        return BOOT_UART_FRAME_STATUS_INVALID_ARGUMENT;
    }
    /* 获取已占用时间 */
    const uint32_t elapsed = HAL_GetTick() - start_tick;
    /* 判断是否超时 */
    if (elapsed >= timeout_ms) {
        return BOOT_UART_FRAME_STATUS_TIMEOUT;
    }
    /* 获取剩余时间 */
    const uint32_t remaining = timeout_ms - elapsed;
    /* 使用剩余时间继续接收数据流 */
    const boot_uart_status_t uart_status = boot_uart_receive(data, length, remaining);
    /* 返回接收状态 */
    switch (uart_status) {
        case BOOT_UART_STATUS_OK:
            return BOOT_UART_FRAME_STATUS_OK;

        case BOOT_UART_STATUS_TIMEOUT:
            return BOOT_UART_FRAME_STATUS_TIMEOUT;

        case BOOT_UART_STATUS_INVALID_ARGUMENT:
        case BOOT_UART_STATUS_BUSY:
        case BOOT_UART_STATUS_ERROR:
        default:
            return BOOT_UART_FRAME_STATUS_TRANSPORT_ERROR;
    }
}

/* 接收数据流中的有效帧放入encode中 */
boot_uart_frame_status_t boot_uart_frame_receive(uint8_t *encoded, size_t encoded_capacity, size_t *encoded_length, uint32_t timeout_ms) {
    /* 判断入参是否合法 */
    if (encoded_length == NULL) {
        return BOOT_UART_FRAME_STATUS_INVALID_ARGUMENT;
    }
    *encoded_length = 0U;
    if (encoded == NULL || timeout_ms == 0U) {
        return BOOT_UART_FRAME_STATUS_INVALID_ARGUMENT;
    }
    if (encoded_capacity < BOOT_PROTOCOL_MIN_FRAME_SIZE) {
        return BOOT_UART_FRAME_STATUS_BUFFER_TOO_SMALL;
    }
    /* 准备接收数据流 */
    uint32_t start_tick = HAL_GetTick();
    uint8_t byte, saw_sof0 = 0;
    boot_uart_frame_status_t state;
    /* 只要没有接收到完整的SOF就一直接收，直到超时或发生异常 */
    while (1) {
        state = boot_uart_frame_receive_exact(&byte, 1, start_tick, timeout_ms);
        if (state != BOOT_UART_FRAME_STATUS_OK) {
            return state;
        }
        /* if this flag is 0, SOF0 is not received and we need judge what is this byte  */
        if(!saw_sof0) {
            saw_sof0 = (byte == BOOT_PROTOCOL_SOF0);
        }
        /* if we come in this branch it express the SOF0 is received */
        else if (byte == BOOT_PROTOCOL_SOF1) {
            break;
        }
        /* And if we come in this branch it express we received SOF0 but second byte is not SOF1.
         * SO we need repeat to judge what is received byte just recently
         */
        else {
            saw_sof0 = (byte == BOOT_PROTOCOL_SOF0);
        }
    }
    encoded[0] = BOOT_PROTOCOL_SOF0;
    encoded[1] = BOOT_PROTOCOL_SOF1;
    /* 如果接收到SOF 则用剩余时间开始接收剩余的固定六个字节 */
    state = boot_uart_frame_receive_exact(&encoded[BOOT_PROTOCOL_SOF_SIZE], 6U, start_tick, timeout_ms);
    if (state != BOOT_UART_FRAME_STATUS_OK) {
        return state;
    }
    /* 解析payload_lenght 计算frame_lenght */
    uint16_t payload_length = (uint16_t)encoded[BOOT_PROTOCOL_LENGTH_OFFSET] | (uint16_t)encoded[BOOT_PROTOCOL_LENGTH_OFFSET + 1U] << 8U;
    if (payload_length > BOOT_PROTOCOL_MAX_PAYLOAD_SIZE) {
        return BOOT_UART_FRAME_STATUS_INVALID_LENGTH;
    }
    size_t frame_lenght = BOOT_PROTOCOL_FIXED_HEADER_SIZE + (size_t)payload_length + BOOT_PROTOCOL_CRC_SIZE;
    /* 检查buffer大小 */
    if (encoded_capacity < frame_lenght) {
        return BOOT_UART_FRAME_STATUS_BUFFER_TOO_SMALL;
    }
    /* 如果长度正常就开始接收data */
    state = boot_uart_frame_receive_exact(&encoded[BOOT_PROTOCOL_FIXED_HEADER_SIZE], (size_t)payload_length + BOOT_PROTOCOL_CRC_SIZE, start_tick, timeout_ms);
    if (state != BOOT_UART_FRAME_STATUS_OK) {
        return state;
    }
    *encoded_length = frame_lenght;
    return BOOT_UART_FRAME_STATUS_OK;
}