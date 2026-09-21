#ifndef BOOT_UART_H
#define BOOT_UART_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    BOOT_UART_STATUS_OK = 0,
    BOOT_UART_STATUS_INVALID_ARGUMENT,
    BOOT_UART_STATUS_TIMEOUT,
    BOOT_UART_STATUS_BUSY,
    BOOT_UART_STATUS_ERROR
} boot_uart_status_t;

boot_uart_status_t boot_uart_transmit(const uint8_t *data, size_t length, uint32_t timeout_ms);
boot_uart_status_t boot_uart_receive(uint8_t *data, size_t length, uint32_t timeout_ms);

#endif