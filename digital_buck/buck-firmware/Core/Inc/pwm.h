/**
 ******************************************************************************
 * @file    pwm.h
 * @brief   PWM 模块：TIM1_CH1(PA8) 上管 + CH1N(PB13) 下管互补输出，带死区
 ******************************************************************************
 */
#ifndef __PWM_H
#define __PWM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/*============================== 用户配置区 ==============================*/
/* 上管 PWM：TIM1_CH1（PA8） */
#define PWM_HIGH_PORT   GPIOA
#define PWM_HIGH_PIN    GPIO_PIN_8

/* 下管 PWM：TIM1_CH1N（PB13，互补输出，硬件自动插入死区） */
#define PWM_LOW_PORT    GPIOB
#define PWM_LOW_PIN     GPIO_PIN_13

/* 开关频率（Hz）。TIM1 时钟 = APB2 = 72MHz（PLL×9）。
 * 100kHz → ARR=719，占空比分辨率 1/720 ≈ 0.139%（≈0.1%，满足需求）
 * 如需严格 0.1% 分辨率，可改为 72000Hz → ARR=999（分辨率 0.1001%）。
 * 电感 L1=68uH 下纹波：ΔI_L = Vout·(1-D)/(f·L)，100kHz 时约 0.43A（3A 的 14%）。 */
#define PWM_FREQ_HZ         100000UL

/* 死区时间（ns）。DTG = t_dead / t_TIM = 100ns / (1/72MHz) = 7.2 → 7
 * 实际死区 = 7 × 13.89ns ≈ 97.2ns ≈ 100ns */
#define PWM_DEADTIME_NS     100UL

/* 最大占空比：防止互补通道在接近 100% 时输出异常，同时保留死区裕量 */
#define PWM_MAX_DUTY        0.95f
/*=======================================================================*/

extern TIM_HandleTypeDef htim1;

void    PWM_Init(void);                 /* TIM1 互补 PWM + 死区 + 更新中断 */
void    PWM_Enable(void);               /* 打开主输出 MOE */
void    PWM_Disable(void);              /* 关闭输出（MOE=0，输出安全低电平） */
void    PWM_SetDuty(float duty);        /* 0.0 ~ PWM_MAX_DUTY */
float   PWM_GetDuty(void);
uint32_t PWM_GetArr(void);              /* 当前 ARR（按实际 PCLK2 计算） */

#ifdef __cplusplus
}
#endif

#endif /* __PWM_H */
