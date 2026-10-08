/**
 ******************************************************************************
 * @file    adc.c
 * @brief   ADC1 四通道扫描 + DMA;TIM1 TRGO 触发,每 PWM 周期一轮
 *
 * 分工(这是本工程"把浮点移出中断"的关键):
 *   100kHz 中断里:只读【电流】那一路 -> 整数限流比较 + 交给控制环
 *   主循环里      :Vin / Vout / NTC 的浮点换算与平滑(慢变量,不在乎几百 us)
 ******************************************************************************
 */
#include <math.h>
#include "main.h"
#include "adc.h"

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

/* DMA 环形缓冲:一轮 4 个通道,rank1 = 电流(最先转换) */
static volatile uint16_t s_raw[ADC_NBR_CONV];

static volatile uint16_t s_zero_raw = 467U;   /* 零点码值,上电标定 */
static volatile float    s_iout     = 0.0f;
static volatile float    s_vin      = 0.0f;
static volatile float    s_vout     = 0.0f;
static volatile float    s_ntc_v    = 0.0f;
static volatile float    s_temp_c   = -100.0f;
static volatile uint32_t s_scan_cnt = 0U;     /* 扫描计数(标定/诊断用) */

/*==================== 初始化 ====================*/
static void ADC_GpioInit(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  g.Pin  = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &g);
}

static void ADC_DmaInit(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  hdma_adc1.Instance                 = DMA1_Channel1;
  hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
  hdma_adc1.Init.Mode                = DMA_CIRCULAR;
  hdma_adc1.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
  if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) Error_Handler();
  __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

  /* 不用 DMA 中断:控制跑在 TIM1 更新中断里,直接读缓冲 */
}

void ADC_Init(void)
{
  ADC_ChannelConfTypeDef cc = {0};

  __HAL_RCC_ADC1_CLK_ENABLE();
  __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);      /* ADCCLK = 72/6 = 12MHz */

  ADC_GpioInit();
  ADC_DmaInit();

  hadc1.Instance                   = ADC1;
  hadc1.Init.ScanConvMode          = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode    = ENABLE;    /* 自由连续扫描,不依赖任何触发源 */
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;   /* 软件启动 */
  /* 注:F1 的 ADC_InitTypeDef 【没有】 DMAContinuousRequests 字段(那是 F4/G4 的),
   *    缓冲区连续搬运由 DMA 自己的 CIRCULAR 模式保证,不需要设它。 */
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion       = ADC_NBR_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

  /* rank1 = 电流:最先转换 */
  cc.SamplingTime = ADC_SAMPLE_TIME;
  cc.Channel = ADC_IOUT_CHANNEL; cc.Rank = 1U;
  if (HAL_ADC_ConfigChannel(&hadc1, &cc) != HAL_OK) Error_Handler();
  cc.Channel = ADC_VIN_CHANNEL;  cc.Rank = 2U;
  if (HAL_ADC_ConfigChannel(&hadc1, &cc) != HAL_OK) Error_Handler();
  cc.Channel = ADC_VOUT_CHANNEL; cc.Rank = 3U;
  if (HAL_ADC_ConfigChannel(&hadc1, &cc) != HAL_OK) Error_Handler();
  cc.Channel = ADC_NTC_CHANNEL;  cc.Rank = 4U;
  if (HAL_ADC_ConfigChannel(&hadc1, &cc) != HAL_OK) Error_Handler();

  /* F103 的 ADC 没有校准寄存器(CAL 是 F2/F4 才有),不需要校准 */

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_raw, ADC_NBR_CONV) != HAL_OK)
  {
    Error_Handler();
  }
}

/*==================== 零点标定 ====================*/
/* 调用时输出必须已关断(PWM 关、负载电流 0)。
 * 等若干轮扫描拿"新数据",再平均,避免用到启动前的 0 值。 */
void ADC_CalibrateZero(void)
{
  uint32_t i;
  uint32_t sum = 0U;
  uint32_t start = s_scan_cnt;

  /* 先等 4 轮扫描,确保 DMA 已经在搬真数据 */
  while ((s_scan_cnt - start) < 4U) { }
  start = s_scan_cnt;

  for (i = 0U; i < ADC_IOUT_CAL_SAMPLES; i++)
  {
    uint32_t t = s_scan_cnt;
    while (s_scan_cnt == t) { }        /* 等下一轮扫描 */
    sum += s_raw[0];
  }
  s_zero_raw = (uint16_t)(sum / ADC_IOUT_CAL_SAMPLES);

  s_iout = 0.0f;
}

/*==================== 取值 ====================*/
uint16_t ADC_GetIoutRaw(void) { return s_raw[0]; }
uint16_t ADC_GetZeroRaw(void) { return s_zero_raw; }
float    ADC_GetIout(void)    { return s_iout; }
float    ADC_GetVin(void)     { return s_vin; }
float    ADC_GetVout(void)    { return s_vout; }
float    ADC_GetNtcVolt(void) { return s_ntc_v; }

float ADC_GetTempC(void)
{
  return s_temp_c;
}

/*==================== 电流换算(给 100kHz 中断用)====================
 * 用【标定后的零点】扣偏置,而不是用理论值 0.3765V,
 * 这样 LMV321 的输入失调(+/-7mV -> 约 +/-233mA)被消掉。
 * 只做 2 次浮点运算,中断里负担很小。
 */
float ADC_ConvertIout(uint16_t raw)
{
  float dv = ((float)raw - (float)s_zero_raw) * ADC_LSB_V;
  return dv / ADC_IOUT_V_PER_A;
}

/* 100kHz 中断里调:更新电流并记扫描计数 */
void ADC_OnPwmTick(void)
{
  s_iout = ADC_ConvertIout(s_raw[0]);
  s_scan_cnt++;
  if (s_scan_cnt == 0U) s_scan_cnt = 1U;
}

/*==================== 慢变量(主循环)====================
 * Vin / Vout / NTC 都是慢变量,放到主循环算浮点,并做一阶平滑。
 * 平滑系数 1/8:约 8 个主循环周期收敛。
 */
void ADC_UpdateSlow(void)
{
  static float vin_f = 0.0f, vout_f = 0.0f, ntc_f = 0.0f;
  static uint16_t s_ntc_dec = 0U;
  static uint8_t primed = 0U;

  float vin  = (float)s_raw[1] * ADC_LSB_V * ADC_VIN_DIV;
  float vout = (float)s_raw[2] * ADC_LSB_V / ADC_VOUT_DIV;
  float ntc  = (float)s_raw[3] * ADC_LSB_V;

  if (primed == 0U)
  {
    vin_f = vin; vout_f = vout; ntc_f = ntc; primed = 1U;
  }
  else
  {
    vin_f  += (vin  - vin_f)  * 0.125f;
    vout_f += (vout - vout_f) * 0.125f;
    ntc_f  += (ntc  - ntc_f)  * 0.125f;
  }

  s_vin   = vin_f;
  s_vout  = vout_f;
  s_ntc_v = ntc_f;

  /* ---- NTC -> 温度(比例式,与 Vin 无关)----
   * [!] logf 很贵,而温度是热学慢变量 => 每 100 次调用才真正算一次,
   *     其余时候沿用上次结果。这样 Vin/Vout 可以每次都刷新(电压环要新鲜数据),
   *     又不会把 CPU 花在 logf 上。 */
  s_ntc_dec++;
  if (s_ntc_dec < 100U) return;
  s_ntc_dec = 0U;
  if (vin_f > 1.0f && ntc_f > 0.002f)
  {
    float ratio = ntc_f / vin_f;              /* = R30/(R_NTC+R30) */
    if (ratio > 0.0005f && ratio < 0.9995f)
    {
      float rntc = ADC_NTC_R30 * (1.0f / ratio - 1.0f);
      if (rntc > 1.0f)
      {
        float tk = 1.0f / (1.0f / ADC_NTC_T25_K + logf(rntc / ADC_NTC_R25) / ADC_NTC_B);
        s_temp_c = tk - 273.15f;
      }
      else
      {
        s_temp_c = 200.0f;                    /* NTC 近似短路 => 报高温 */
      }
    }
    else
    {
      s_temp_c = -100.0f;                     /* 比例异常 => 无效 */
    }
  }
  else
  {
    s_temp_c = -100.0f;
  }
}
