/**
 ******************************************************************************
 * @file    main.h
 * @brief   数字降压电源 (Digital Buck Converter) 主头文件
 *          基于 STM32F103C8T6 + STM32 HAL 库(F1 系列)
 *
 *          规格(与原理图/PCB 一致,由用户 2026-10-08 定):
 *            输入 15 ~ 25V
 *            输出  5 ~ 10V,<= 2A(限流 2A,过流跳闸 3.2A)
 *            拓扑:同步半桥(RT9624AZS 驱动 + XR20N04 x2),L1 = 68uH
 *            控制:电压外环(5kHz)+ 电流内环(50kHz)双闭环 PID
 *            开关频率 100kHz(TIM1_CH3 = PA10,单路 --> RT9624)
 *
 * [!] 本工程【不是 CubeMX 生成的】:所有外设初始化都在各模块里,
 *     没有 USER CODE 块,也不需要用 CubeMX 重新生成。
 * [!] 本板没有串口(PA9 悬空、PA10 被 PWM 占用) => uart.c 不参与编译。
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
#include "key.h"
#include "protect.h"
#include "storage.h"

/*============================== 用户配置区 ==============================*/


/* 工程规格(改这里只影响注释与上限检查,真正的限幅在 protect.h / pid.h) */
#define BUCK_VIN_MIN_V      15.0f
#define BUCK_VIN_MAX_V      25.0f
#define BUCK_VOUT_MIN_V     5.0f
#define BUCK_VOUT_MAX_V     10.0f
/*=======================================================================*/

void SystemClock_Config(void);
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
