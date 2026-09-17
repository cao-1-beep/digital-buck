/**
 ******************************************************************************
 * @file    adc.c
 * @brief   ADC1 三通道连续扫描 + DMA 环形缓冲，CPU 零干预
 *
 * 采样链路（换算公式，均基于宏定义，改硬件参数只需改 adc.h）：
 *   Vout = raw × VREF / 4096 / 分压比
 *   Iout = raw × VREF / 4096 / (采样电阻 × 放大增益)
 *   Vin  = raw × VREF / 4096 / 分压比
 *
 * 时序：ADC 时钟 = PCLK2/6 = 12MHz，每通道 13.5+12.5 = 26 周期 ≈ 2.2us，
 * 三通道一轮 ≈ 6.5us，连续扫描速率 ≈ 154kHz，每个 PWM 周期（10us）都有新数据。
 * 控制环读取最近 ADC_AVG 轮的均值，等效对开关纹波做滑动平均。
 ******************************************************************************
 */
#include "main.h"

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

/* DMA 环形缓冲：每轮 3 通道 × ADC_AVG 轮 */
static uint16_t s_raw[ADC_AVG * 3];

/* 预计算的换算系数（初始化时算好，控制环内不再做除法） */
static float s_scale_vout, s_scale_iout, s_scale_vin;

void ADC_Init(void)
{
  GPIO_InitTypeDef      g = {0};
  ADC_ChannelConfTypeDef c = {0};

  __HAL_RCC_ADC1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* PA0/PA1/PA2 模拟输入 */
  g.Pin  = ADC_VOUT_PIN | ADC_IOUT_PIN | ADC_VIN_PIN;
  g.Mode = GPIO_MODE_ANALOG;
  HAL_GPIO_Init(ADC_PORT, &g);

  /* DMA1 通道 1（ADC1 专用），环形模式，半字对齐（ADC 数据 12bit） */
  hdma_adc1.Instance                 = DMA1_Channel1;
  hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
  hdma_adc1.Init.Mode                = DMA_CIRCULAR;
  hdma_adc1.Init.Priority            = DMA_PRIORITY_HIGH;
  if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) Error_Handler();
  __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

  /* ADC1：扫描模式，连续转换，软件触发 */
  hadc1.Instance                   = ADC1;
  hadc1.Init.ScanConvMode          = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode    = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion       = 3;
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV6;   /* 12MHz */
  /* DDS=1：扫描完成后继续产生 DMA 请求，配合 CIRCULAR 实现连续搬运 */
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

  /* 通道配置：PA0→IN0，PA1→IN1，PA2→IN2 */
  c.Channel      = ADC_CHANNEL_0;
  c.Rank         = 1;
  c.SamplingTime = ADC_SAMPLE_TIME;
  if (HAL_ADC_ConfigChannel(&hadc1, &c) != HAL_OK) Error_Handler();
  c.Channel = ADC_CHANNEL_1;
  c.Rank    = 2;
  if (HAL_ADC_ConfigChannel(&hadc1, &c) != HAL_OK) Error_Handler();
  c.Channel = ADC_CHANNEL_2;
  c.Rank    = 3;
  if (HAL_ADC_ConfigChannel(&hadc1, &c) != HAL_OK) Error_Handler();

  /* 换算系数 */
  s_scale_vout = ADC_VREF / 4096.0f / ADC_VOUT_DIVIDER;
  s_scale_iout = ADC_VREF / 4096.0f / (ADC_RSHUNT_OHM * ADC_AMP_GAIN);
  s_scale_vin  = ADC_VREF / 4096.0f / ADC_VIN_DIVIDER;

  /* 注意：F103 的 ADC 无校准寄存器（CAL 为 F2/F4 才有的功能），无需校准。
   * 偏移误差（约几 LSB）可在标定时通过 ADC_AMP_GAIN/分压比微调补偿 */

  /* 启动连续转换（DMA 环形搬运，无需中断） */
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_raw, (uint32_t)(ADC_AVG * 3)) != HAL_OK)
    Error_Handler();
}

/* 读取三路平均值（一次调用，供 100kHz 控制环使用，开销极小） */
void ADC_GetMeasurements(float *vout, float *iout, float *vin)
{
  uint32_t s0 = 0, s1 = 0, s2 = 0;
  uint16_t i;

  for (i = 0; i < ADC_AVG; i++) {
    s0 += s_raw[i * 3 + 0];
    s1 += s_raw[i * 3 + 1];
    s2 += s_raw[i * 3 + 2];
  }
  *vout = (float)s0 * s_scale_vout * (1.0f / (float)ADC_AVG);
  *iout = (float)s1 * s_scale_iout * (1.0f / (float)ADC_AVG);
  *vin  = (float)s2 * s_scale_vin  * (1.0f / (float)ADC_AVG);
}

float ADC_GetVout(void)
{
  float v, i, vi;
  ADC_GetMeasurements(&v, &i, &vi);
  return v;
}

float ADC_GetIout(void)
{
  float v, i, vi;
  ADC_GetMeasurements(&v, &i, &vi);
  return i;
}

float ADC_GetVin(void)
{
  float v, i, vi;
  ADC_GetMeasurements(&v, &i, &vi);
  return vi;
}
