/**
 ******************************************************************************
 * @file    adc.h
 * @brief   ADC 模块:4 通道扫描 + DMA,通道按原理图网络名对齐
 *
 * 通道(全部 GPIOA,来自 buck.SchDoc + buck.PcbDoc 双证):
 *   PA6 = CUR_ADC  = U2(LMV321 电流放大器)输出
 *   PA5 = VIN_ADC  = U5B 缓冲后的 Vin 1:11 分压
 *   PA7 = VFB_ADC  = U3 缓冲后的 Vout 分压(你已把 R20 改成 3.3K)
 *   PA4 = TEP_ADC  = U5A 缓冲后的外部 NTC(P5 排针)
 *
 * 触发:软件启动 + 连续扫描(不依赖任何外部触发源)。
 *   为什么不需要跟 PWM 同步:采样电阻 R11 在【负载回流路径】
 *   (P2.2 -> R11 -> GND),旁路电容的纹波走 GND 不经过它
 *   => 采到的是【干净的直流负载电流】,不含电感纹波,采样时刻无所谓。
 *
 * [!] 与代码原版的差别:
 *   原版 PA0/PA1/PA2(Vout/Iout/Vin)三个脚,在本板上是【按键 PA0~PA3】,
 *   而且原版没有 NTC 这一路。
 ******************************************************************************
 */
#ifndef __ADC_H
#define __ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/*============================== 用户配置区 ==============================*/
#define ADC_IOUT_CHANNEL    ADC_CHANNEL_6    /* PA6 */
#define ADC_VIN_CHANNEL     ADC_CHANNEL_5    /* PA5 */
#define ADC_VOUT_CHANNEL    ADC_CHANNEL_7    /* PA7 */
#define ADC_NTC_CHANNEL     ADC_CHANNEL_4    /* PA4 */

#define ADC_NBR_CONV        4U
#define ADC_SAMPLE_TIME     ADC_SAMPLETIME_13CYCLES_5
/* 一轮扫描 = 4 x (13.5+12.5) = 104 个 ADC 周期 @12MHz = 8.67us
 * < 10us(PWM 周期) => 每个周期都能拿到一轮新数据 */

#define ADC_VREF            3.3f
#define ADC_FULL_SCALE      4096.0f
#define ADC_LSB_V           (ADC_VREF / ADC_FULL_SCALE)   /* 0.8057 mV/码 */

/* --- 电流(U2 = LMV321 差分放大,R11 = 30mR,R19 = 30K)---
 * 增益 = 1 + R19/(2*R18) = 1 + 30k/2k = 16;输入分压再乘 0.98
 *   => 采样点到输出 15.7 V/V,灵敏度 = 15.7 x 0.030 = 0.4706 V/A
 * 偏置 = 16 x 23.5mV = 0.3765V(1.2V 基准经 R3(100K)/R5(1k) 注入后被放大)
 *   => I[A] = (V_adc - 0.3765) / 0.4706
 *   => 零电流 ADC 约 467;量程 -0.80A ~ +6.21A
 * 上电会做零点标定,所以偏置的绝对误差不累积 */
#define ADC_IOUT_ZERO_V     0.3765f
#define ADC_IOUT_V_PER_A    0.4706f
#define ADC_IOUT_CAL_SAMPLES 8U      /* 零点标定取几轮扫描的平均 */

/* --- 输入电压:R21 = 10k / R23 = 1k => 1:11,满量程 36.3V --- */
#define ADC_VIN_DIV         11.0f

/* --- 输出电压:R6 = 10K / R20 = 3.3K => 0.2481,满量程 13.3V --- */
#define ADC_VOUT_RTOP       10000.0f
#define ADC_VOUT_RBOT       3300.0f
#define ADC_VOUT_DIV        (ADC_VOUT_RBOT / (ADC_VOUT_RTOP + ADC_VOUT_RBOT))

/* --- NTC:MF52A103F3950,R30 改成 68R ---
 * VIN -> NTC -> P5.2 -> R30(68R) -> GND -> U5A 跟随器 -> PA4
 * ratio = V_ntc / Vin = R30 / (R_NTC + R30)   (与 Vin 无关,比例式)
 * R_NTC = R30 x (1/ratio - 1)
 * T[K]  = 1 / (1/298.15 + ln(R_NTC/R25)/B)
 * [!] 温度升高时 V_ntc 上升;0C 以下分辨率很差,只拿它做过温,不做低温 */
#define ADC_NTC_R30         68.0f
#define ADC_NTC_R25         10000.0f
#define ADC_NTC_B           3950.0f
#define ADC_NTC_T25_K       298.15f
/*=======================================================================*/

extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;

void     ADC_Init(void);                 /* 时钟/DMA/4通道 + 启动 + 零点标定 */
void     ADC_CalibrateZero(void);        /* 输出关断时采零点(必做) */
uint16_t ADC_GetIoutRaw(void);           /* 电流通道原始码值(给逐周期限流) */
float    ADC_GetIout(void);              /* 电流 (A) —— 由 100kHz 中断更新 */
float    ADC_GetVin(void);               /* 输入电压 (V) */
float    ADC_GetVout(void);              /* 输出电压 (V) */
float    ADC_GetNtcVolt(void);           /* NTC 节点电压 (V) */
float    ADC_GetTempC(void);             /* 由 NTC 反算的温度 (C),算不出返回 -100 */
uint16_t ADC_GetZeroRaw(void);           /* 标定得到的零点码值(诊断用) */
void     ADC_UpdateSlow(void);           /* 主循环调用:平滑 + 换算慢变量 */
void     ADC_OnPwmTick(void);           /* 100kHz 中断里调:更新电流 + 扫描计数 */

#ifdef __cplusplus
}
#endif

#endif /* __ADC_H */
