#include <stdbool.h>

#include "stm32f4xx_hal.h"
#include "boot_jump.h"
#include "boot_memory.h"

/* 停止并清理 SysTick */
static void boot_stop_systick(void) {
    /* 停止 SysTick 计数和 SysTick 中断 */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    /* 清除已经 pending 的 SysTick 和 PendSV */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
}

/* 禁用所有外部中断并清除 pending 状态 */
static void boot_disable_and_clear_nvic(void) {
    /* 读取硬件实现了多少组 NVIC 寄存器 */
    const uint32_t register_count = ((SCnSCB->ICTR & SCnSCB_ICTR_INTLINESNUM_Msk) >> SCnSCB_ICTR_INTLINESNUM_Pos) + 1U;
    /* 依次关闭和清除这些寄存器所控制的 IRQ 和 IRQ的pending (W1C Write 1 to Clear) */
    for (uint32_t index = 0U; index < register_count; ++index) {
        NVIC->ICER[index] = 0xFFFFFFFFU;
        NVIC->ICPR[index] = 0xFFFFFFFFU;
    }
}

/* 恢复基础时钟配置并复位 AHB/APB 外设 */
static bool boot_deinit_hal(void) {
    if (HAL_RCC_DeInit() != HAL_OK) {
        return false;
    }
    if (HAL_DeInit() != HAL_OK) {
        return false;
    }
    return true;
}

/* 将异常向量表切换到 APP */
static void boot_set_app_vector_table(void) {
    /* 重新定向VTOR 即app的向量表 */
    SCB->VTOR = (uint32_t)BOOT_APP_FLASH_START;

    /* 确保寄存器写入在继续执行前生效 */
    __DSB();
    /* 刷新指令流水线，使后续指令在新的系统状态下重新取指执行 */
    __ISB();
}

__attribute__((naked, __noreturn__))
static void boot_handoff(uint32_t initial_msp, uint32_t reset_handler) {
    __asm volatile(
        /* 恢复为特权线程模式，并选择 MSP */
        "movs r2, #0       \n"
        "msr control, r2   \n"

        /* 清除中断与 Fault 的优先级屏蔽 */
        "msr basepri, r2   \n"
        "msr faultmask, r2 \n"

        /* 从这里开始，栈已经属于 APP */
        "msr msp, r0       \n"

        /* 恢复普通可屏蔽中断 */
        "msr primask, r2   \n"

        /* 让特殊寄存器的修改对后续执行立即生效 */
        "isb               \n"

        /* 跳转到 APP Reset_Handler，不保存返回地址 */
        "bx r1             \n"
    );
}

noreturn void boot_jump_to_application(const boot_image_vector_table_t *vector_table) {
    /* 趁 Bootloader 环境仍完整，先保存已经验证过的 APP 入口信息 后续不再读取 vector_table */
    const uint32_t initial_msp = vector_table->initial_msp;
    const uint32_t reset_handler = vector_table->reset_handler;

    /* HAL_RCC_DeInit() 可能依赖 HAL tick，因此必须在关闭中断和停止 SysTick 之前执行 */
    if (!boot_deinit_hal()) {
        NVIC_SystemReset();
    }

    /* 从此处开始禁止普通中断，避免清理期间进入Bootloader 的中断处理函数 */
    __disable_irq();
    boot_stop_systick();
    boot_disable_and_clear_nvic();

    /* NVIC 已清理、普通中断已屏蔽后，再把异常向量表交给 APP */
    boot_set_app_vector_table();

    /* 进入裸汇编函数：恢复特殊寄存器、设置 APP MSP，最后跳转 Reset_Handler */
    boot_handoff(initial_msp, reset_handler);
}