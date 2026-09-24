#ifndef BOOT_MEMORY_H
#define BOOT_MEMORY_H

#include <stdint.h>

/* These symbols are defined by STM32F407xx_FLASH.ld. */
extern uint8_t __app_flash_start__;
extern uint8_t __app_flash_end__;
extern uint8_t __metadata_flash_start__;
extern uint8_t __metadata_flash_end__;
extern uint8_t __device_flash_end__;

#define BOOT_APP_FLASH_START ((uintptr_t)&__app_flash_start__)
#define BOOT_APP_FLASH_END ((uintptr_t)&__app_flash_end__)
#define BOOT_METADATA_FLASH_START ((uintptr_t)&__metadata_flash_start__)
#define BOOT_METADATA_FLASH_END ((uintptr_t)&__metadata_flash_end__)
#define BOOT_DEVICE_FLASH_END ((uintptr_t)&__device_flash_end__)

/* STM32F407VGT6 SRAM1 and SRAM2 form one contiguous 128 KiB region. */
#define BOOT_SRAM_START       0x20000000U
#define BOOT_SRAM_END         0x20020000U

/* The Cortex-M4 core can also use the 64 KiB CCM RAM for its stack. */
#define BOOT_CCM_RAM_START    0x10000000U
#define BOOT_CCM_RAM_END      0x10010000U

#endif /* BOOT_MEMORY_H */
