/**
 ******************************************************************************
 * @file    protect.h
 * @brief   保护模块：过流 / 过压 / 欠压检测与故障锁定
 ******************************************************************************
 */
#ifndef __PROTECT_H
#define __PROTECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>

/*============================== 用户配置区 ==============================*/
/* 过流保护：连续 OCP_DEBOUNCE 个控制周期超限才触发（去抖） */
#define OCP_DEBOUNCE    3

/* 过压保护：Vout > VSET×(1+OVP_RATIO)，或超过绝对上限 OVP_ABS_MAX */
#define OVP_RATIO       0.10f
#define OVP_ABS_MAX     10.8f
#define OVP_ABS_MIN     1.0f     /* 低压设定时 OVP 阈值下限 */
#define OVP_DEBOUNCE    3

/* 欠压保护（置 0 关闭）：Vin < UVP_THRESHOLD 锁定；
 * Vin 恢复到 UVP_THRESHOLD+UVP_HYST 后自动清除该位（输出不会自动开启） */
#define ENABLE_UVP      1
#define UVP_THRESHOLD   10.0f
#define UVP_HYST        0.5f
#define UVP_DEBOUNCE    50

/* 故障标志位 */
#define FAULT_OCP       0x01
#define FAULT_OVP       0x02
#define FAULT_UVP       0x04
#define FAULT_EXT_OC    0x08
#define FAULT_EXT_OV    0x10

/* ---- 外部比较器保护（可选功能，默认关闭）----
 * 硬件比较器（U2/U3 LMV321）输出接 EXTI 引脚，触发即锁断输出。
 * 仅支持 PA/PB 口、EXTI 线 0~4（GPIOA/GPIOB 时钟已由其他模块使能）。
 * 启用方法：取消注释并填写引脚定义：
#define PROTECT_USE_EXT_COMP   1
#define OCP_CMP_PORT           GPIOA
#define OCP_CMP_PIN            GPIO_PIN_3
#define OCP_CMP_PIN_IDX        3
#define OVP_CMP_PORT           GPIOA
#define OVP_CMP_PIN            GPIO_PIN_4
#define OVP_CMP_PIN_IDX        4
#define PROTECT_EXT_MODE       GPIO_MODE_IT_FALLING
*/
/*=======================================================================*/

extern volatile uint8_t g_fault;

void  Protect_Init(void);
void  Protect_Check(float vout, float iout, float vin); /* 每个控制周期调用 */
void  Protect_ClearFaults(void);
void  Protect_SetOcp(float a);
float Protect_GetOcp(void);

#ifdef __cplusplus
}
#endif

#endif /* __PROTECT_H */
