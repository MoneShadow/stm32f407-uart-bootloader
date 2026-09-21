#include <stdint.h>

#include "usart.h"
#include "boot_uart.h"

static boot_uart_status_t boot_uart_convert_hal_status(HAL_StatusTypeDef hal_status) {
    switch (hal_status) {
        case HAL_OK:
            return BOOT_UART_STATUS_OK;

        case HAL_TIMEOUT:
            return BOOT_UART_STATUS_TIMEOUT;

        case HAL_BUSY:
            return BOOT_UART_STATUS_BUSY;

        case HAL_ERROR:
        default:
            return BOOT_UART_STATUS_ERROR;
    }
}

boot_uart_status_t boot_uart_transmit(const uint8_t *data, size_t length, uint32_t timeout_ms) {
    /* 先检查data length */
    if (data == NULL || length == 0U || length > UINT16_MAX) {
        return BOOT_UART_STATUS_INVALID_ARGUMENT;
    }
    HAL_StatusTypeDef state = HAL_UART_Transmit(&huart1, data, (uint16_t)length, timeout_ms);
    return boot_uart_convert_hal_status(state);
}

boot_uart_status_t boot_uart_receive(uint8_t *data, size_t length, uint32_t timeout_ms) {
    /* 先检查data length */
    if (data == NULL || length == 0U || length > UINT16_MAX) {
        return BOOT_UART_STATUS_INVALID_ARGUMENT;
    }
    HAL_StatusTypeDef state = HAL_UART_Receive(&huart1, data, (uint16_t)length, timeout_ms);
    return boot_uart_convert_hal_status(state);
}
