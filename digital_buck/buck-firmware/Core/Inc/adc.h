/**
 ******************************************************************************
 * @file    adc.h
 * @brief   ADC 模块：3 通道扫描 + DMA 环形缓冲（连续转换）
 ******************************************************************************
 */
#ifndef __ADC_H
#define __ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/*============================== 用户配置区 ==============================*/
/* 采样通道（全部在 GPIOA，模拟输入） */
#define ADC_PORT        GPIOA
#define ADC_VOUT_PIN    GPIO_PIN_0    /* 输出电压反馈  ADC1_IN0 */
#define ADC_IOUT_PIN    GPIO_PIN_1    /* 输出电流反馈  ADC1_IN1 */
#define ADC_VIN_PIN     GPIO_PIN_2    /* 输入电压检测  ADC1_IN2 */

/* 基准电压（V） */
#define ADC_VREF        3.3f

/* 输出电压分压比（Vout → ADC 引脚）。
 * 例：10kΩ + 30kΩ 分压 → 0.25，即 10V 输出映射为 2.5V */
#define ADC_VOUT_DIVIDER   0.25f

/* 输入电压分压比（Vin → ADC 引脚），例：10k + 30k → 0.25，12V → 3.0V */
#define ADC_VIN_DIVIDER    0.25f

/* 电流采样：I = Vadc / (Rshunt × Gain)
 * R11 = 10mΩ，运放 U5 增益 50 倍 → 1.5V 对应 3.0A，满量程 6.6A */
#define ADC_RSHUNT_OHM  0.01f
#define ADC_AMP_GAIN    50.0f

/* 每通道平均次数（DMA 缓冲 = 3 × ADC_AVG 个字），抑制开关噪声 */
#define ADC_AVG         4

/* 采样时间：13.5 周期 @12MHz ≈ 1.1us/通道，全扫 ≈ 6.5us（快于 PWM 周期）
 * 若噪声偏大可改为 ADC_SAMPLETIME_28CYCLES_5 或 41CYCLES_5 */
#define ADC_SAMPLE_TIME ADC_SAMPLETIME_13CYCLES_5
/*=======================================================================*/

extern ADC_HandleTypeDef hadc1;

void ADC_Init(void);
void ADC_GetMeasurements(float *vout, float *iout, float *vin); /* 一次取三路均值 */
float ADC_GetVout(void);
float ADC_GetIout(void);
float ADC_GetVin(void);

#ifdef __cplusplus
}
#endif

#endif /* __ADC_H */
