#ifndef BOOT_UART_FRAME_H
#define BOOT_UART_FRAME_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    BOOT_UART_FRAME_STATUS_OK = 0,
    BOOT_UART_FRAME_STATUS_INVALID_ARGUMENT,
    BOOT_UART_FRAME_STATUS_TIMEOUT,
    BOOT_UART_FRAME_STATUS_TRANSPORT_ERROR,
    BOOT_UART_FRAME_STATUS_BUFFER_TOO_SMALL,
    BOOT_UART_FRAME_STATUS_INVALID_LENGTH
} boot_uart_frame_status_t;

boot_uart_frame_status_t boot_uart_frame_receive(uint8_t *encoded, size_t encoded_capacity, size_t *encoded_length, uint32_t timeout_ms);

#endif