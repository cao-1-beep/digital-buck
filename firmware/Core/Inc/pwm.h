/**
 ******************************************************************************
 * @file    pwm.h
 * @brief   PWM 模块:TIM1_CH3 = PA10,【单路】输出 -> RT9624 的 PWM 输入
 *
 * [!] 与代码原版的根本差别(按原理图 + PCB 网表双证改正):
 *   原版:TIM1_CH1(PA8)上管 + TIM1_CH1N(PB13)下管,互补输出 + DTG 死区
 *   实际:板上【只有一路】栅极信号 -- PA10 -> U4.RT9624 脚2(PWM);
 *         PB13 / PB14 / PB15 在 PCB 网表里【全部未连接】。
 *
 *   RT9624AZS 是【单 PWM 输入】的半桥驱动:内部产生 UGATE/LGATE、
 *   内部死区、内置自举二极管、交错导通保护 => 死区由硬件给,软件不设 DTG。
 *
 * 引脚(原理图网络名 PWM):U1.31 = PA10 = TIM1_CH3 -> U4.2
 ******************************************************************************
 */
#ifndef __PWM_H
#define __PWM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/*============================== 用户配置区 ==============================*/
/* 单路 PWM:PA10 = TIM1_CH3 */
#define PWM_GPIO_PORT   GPIOA
#define PWM_GPIO_PIN    GPIO_PIN_10
#define PWM_TIM_CHANNEL TIM_CHANNEL_3

/* 开关频率(Hz)。TIM1 挂 APB2,本工程 APB2 = 72MHz
 * => 100kHz 对应 ARR = 719,占空比分辨率 1/720 = 0.139% */
#define PWM_FREQ_HZ     100000UL

/* 最大占空比上限 0.85
 * 理由不是防直通(死区在驱动芯片里),而是【自举电容的充电时间】--
 * RT9624 靠关断期给自举电容充电;占空比太高(BOOT 一直不落地)会充不满,
 * 上管驱动变弱。15V->10V 时节占空比 66.7%,离这条线还有余量。 */
#define PWM_MAX_DUTY    0.85f
#define PWM_MIN_DUTY    0.00f
/*=======================================================================*/

extern TIM_HandleTypeDef htim1;

void     PWM_Init(void);                /* TIM1 单路 PWM + 100kHz 更新中断 */
void     PWM_Enable(void);              /* 打开主输出 MOE */
void     PWM_Disable(void);             /* 关输出:MOE=0,栅极回安全电平 */
void     PWM_SetDuty(float duty);       /* 0.0 ~ PWM_MAX_DUTY */
float    PWM_GetDuty(void);
uint32_t PWM_GetArr(void);              /* 当前 ARR */
uint32_t PWM_GetTimClk(void);           /* TIM1 输入时钟(Hz) */

#ifdef __cplusplus
}
#endif

#endif /* __PWM_H */
