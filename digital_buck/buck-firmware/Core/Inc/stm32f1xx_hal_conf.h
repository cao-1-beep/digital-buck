/**
 ******************************************************************************
 * @file    stm32f1xx_hal_conf.h
 * @brief   HAL 库配置：模块选择与系统参数
 *          本工程不需要 CubeMX，所有外设初始化均在模块代码内完成。
 *          工程预定义宏：USE_HAL_DRIVER、STM32F103x8
 ******************************************************************************
 */
#ifndef __STM32F1xx_HAL_CONF_H
#define __STM32F1xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ########################## 模块选择 ############################## */
#define HAL_RCC_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* ########################## HSE / HSI 值 ############################# */
/* 外部晶振频率：蓝色药丸等常见 F103C8T6 开发板为 8MHz，请按实际电路修改 */
#if !defined(HSE_VALUE)
#define HSE_VALUE    ((uint32_t)8000000U)
#endif
/* HSE 起振超时（ms），超时后固件自动回退到 HSI（64MHz），不影响运行 */
#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT   100U
#endif

#if !defined(HSI_VALUE)
#define HSI_VALUE    ((uint32_t)8000000U)
#endif

#if !defined(LSI_VALUE)
#define LSI_VALUE    ((uint32_t)40000U)
#endif

/* ########################## 系统参数 ################################# */
#define VDD_VALUE                   3300U
#define TICK_INT_PRIORITY           0x0FU
#define USE_RTOS                    0U
#define PREFETCH_ENABLE             1U

/* ########################## 断言 ###################################### */
/* 调试时可将 USE_FULL_ASSERT 定义为 1 并在 main.c 中实现 assert_failed */
#define assert_param(expr) ((void)0U)

/* ########################## 头文件包含 ################################ */
#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32f1xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32f1xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32f1xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32f1xx_hal_cortex.h"
#endif
#ifdef HAL_ADC_MODULE_ENABLED
#include "stm32f1xx_hal_adc.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32f1xx_hal_flash.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32f1xx_hal_pwr.h"
#endif
#ifdef HAL_TIM_MODULE_ENABLED
#include "stm32f1xx_hal_tim.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
#include "stm32f1xx_hal_uart.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_HAL_CONF_H */
