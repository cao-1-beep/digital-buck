/**
 ******************************************************************************
 * @file    protect.h
 * @brief   保护模块:过流(软件)/ 输入欠压 / 输入过压 / 输出过压 / 过温 + 热降额
 *
 * [!] 与代码原版的根本差别:
 *   原版假设板上有一个"硬件过流比较器"接 EXTI(挂在 PA3/PA4),而且用
 *   ADC 算电流做过流。
 *   实际:这颗板的两颗运放 U2/U3 都用掉了(U2 = 电流放大,U3 = Vout 跟随),
 *   【没有留比较器】=> 过流只能靠软件,由电流环在 100kHz 中断里做
 *   【逐周期限流 + 连续 N 次跳闸】。
 *   PA3/PA4 在本板上是【按键】,不是比较器输入。
 *
 * 保护分层(按响应速度):
 *   ① 逐周期限流  100kHz(10us)  整数比较,本周期占空比压 0
 *   ② 硬跳闸      连续 3 个周期   锁存,需手动清除
 *   ③ 输入/输出/温度  主循环 1ms  带消抖与滞回
 *   ④ 过温降额    70C 起线性收电流上限,85C 锁存跳闸
 ******************************************************************************
 */
#ifndef __PROTECT_H
#define __PROTECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/*============================== 用户配置区 ==============================*/
/* 故障标志位 */
#define FAULT_OCP       0x01U    /* 输出过流(软件) */
#define FAULT_UVP       0x02U    /* 输入欠压 */
#define FAULT_OVP_VIN   0x04U    /* 输入过压 */
#define FAULT_OVP_VOUT  0x08U    /* 输出过压 */
#define FAULT_OTP       0x10U    /* 过温 */

/* --- 过流 ---
 * 电感 L1 = xr1050-680m:额定 3A,【饱和 3.8A】
 * 最坏纹波 25V->10V/2A 时约 0.88A p-p => 半纹波 0.44A
 * 跳闸点 3.2A + 0.44A = 3.64A < 3.8A  => 故障时电感也不饱和
 * 限流点是工作阈值(CC),你定 2A */
#define OCP_LIMIT_DEFAULT   2.0f
#define OCP_LIMIT_MIN       0.1f
#define OCP_LIMIT_MAX       2.0f
#define OCP_TRIP_A          3.2f    /* 默认跳闸点 */
#define OCP_TRIP_MIN        2.0f
#define OCP_TRIP_MAX        3.6f    /* 上限必须 < 电感饱和 3.8A */
#define OCP_DEBOUNCE        3U      /* 连续几个 PWM 周期超跳闸点才算 */

/* --- 输入欠压(Vin 规格 15~25V)--- */
#define UVP_THRESHOLD       13.0f
#define UVP_HYST            0.5f
#define UVP_DEBOUNCE        50U     /* ms */

/* --- 输入过压 --- */
#define OVP_VIN_MAX         26.5f
#define OVP_VIN_HYST        0.5f
#define OVP_VIN_DEBOUNCE    20U     /* ms */

/* --- 输出过压 --- */
#define OVP_VOUT_RATIO      1.10f   /* Vout > VSET x 1.10 */
#define OVP_VOUT_ABS        11.0f   /* 或绝对上限 11.0V(测量量程 13.3V) */
#define OVP_VOUT_DEBOUNCE   3U      /* ms */

/* --- 过温(MF52A103F3950 + R30=68R)--- */
#define OTP_DERATE_C        70.0f   /* 起降额 */
#define OTP_TRIP_C          85.0f   /* 锁存跳闸 */
#define OTP_HYST_C          10.0f   /* 回落到 75C 以下才能清 */
#define OTP_DEBOUNCE        100U    /* ms */

/* 过温降额时的电流上限比例下限(85C 时收到 50%) */
#define OTP_DERATE_FLOOR    0.5f
/*=======================================================================*/

extern volatile uint8_t  g_fault;

/* [!] 下面两个是【原始 ADC 码值】阈值,给 100kHz 中断做整数比较用。
 *     任何浮点都不许出现在那个中断里。由 Protect_Init / Protect_SetOcp 更新。 */
extern volatile uint16_t g_limit_raw;
extern volatile uint16_t g_trip_raw;

void  Protect_Init(void);
void  Protect_ClearFaults(void);

/* 100kHz 中断里调用(中断安全):置故障并立即封锁输出 */
void  Protect_Trip(uint8_t fault);

/* 主循环 1ms 调用:输入/输出/温度检查(带消抖与滞回) */
void  Protect_CheckSlow(float vin, float vout, float temp_c, float vset);

/* 过温降额系数:1.0 = 不降额,0.5 = 收到一半(70~85C 线性) */
float Protect_ThermalScale(void);

/* 限流点(CC 工作阈值,0.1~2.0A)-> 刷新 g_limit_raw */
void  Protect_SetLimit(float a);
float Protect_GetLimit(void);
/* 过流跳闸阈值(2.0~3.6A)-> 刷新 g_trip_raw
 * [!] 名字里的 Ocp 指"跳闸",别和上面的 limit 混;两者都要低于电感饱和 3.8A */
void  Protect_SetOcp(float a);
float Protect_GetOcp(void);
const char *Protect_FaultStr(void);         /* 当前故障的短名,给 OLED 用 */

#ifdef __cplusplus
}
#endif

#endif /* __PROTECT_H */
