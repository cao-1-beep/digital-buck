/**
 ******************************************************************************
 * @file    main.h
 * @brief   数字降压电源 (Digital Buck Converter) 主头文件
 *          基于 STM32F103C8T6 + STM32 HAL 库（F1 系列）
 *
 *          输入 12V，输出 0.8V~10V / 3A，半桥拓扑 + RT9624 驱动，
 *          TIM1 互补 PWM（100kHz，死区 100ns），电压外环 + 电流内环双闭环 PID。
 ******************************************************************************
 */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* 模块头文件 */
#include "pwm.h"
#include "adc.h"
#include "pid.h"
#include "uart.h"
#include "key.h"
#include "protect.h"
#include "storage.h"

/*============================== 用户配置区 ==============================*/
/* 状态指示灯（默认 PC13：蓝色药丸板载 LED，低电平点亮）
 * 逻辑：输出关闭 = 灭；输出开启 = 常亮；故障 = 2Hz 闪烁 */
#define LED_PORT        GPIOC
#define LED_PIN         GPIO_PIN_13
#define LED_ON_LEVEL    GPIO_PIN_RESET
#define LED_OFF_LEVEL   GPIO_PIN_SET

/* 串口周期上报间隔（ms），设为 0 关闭周期上报（仍可用 status 命令查询） */
#define UART_TELEMETRY_MS   1000UL
/*=======================================================================*/

void SystemClock_Config(void);
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
