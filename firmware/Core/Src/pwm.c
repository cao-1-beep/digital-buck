/**
 ******************************************************************************
 * @file    pwm.c
 * @brief   TIM1_CH3(PA10)单路 PWM -- 驱动 RT9624 半桥
 *
 * 为什么不需要软件死区:
 *   RT9624AZS 内部自带死区与交错导通保护,UGATE/LGATE 由它自己产生。
 *   MCU 只送一路 PWM 到 U4.2。所以本模块:
 *     - 不配 DTG(死区寄存器保持 0)
 *     - 不配任何 N 通道(互补)
 *     - PB13/PB14/PB15 保持复位默认,它们在板上是未连接脚
 ******************************************************************************
 */
#include "main.h"
#include "pwm.h"

TIM_HandleTypeDef htim1;

static volatile float s_duty = 0.0f;

/* TIM1 的输入时钟:APB2 预分频 = 1 时就是 PCLK2;
 * 预分频 > 1 时定时器时钟 = PCLK2 x 2(RM0008 的定时器时钟规则) */
uint32_t PWM_GetTimClk(void)
{
  uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
  uint32_t hclk  = HAL_RCC_GetHCLKFreq();

  if (pclk2 == hclk) return pclk2;      /* APB2 不分频 => 定时器时钟 = PCLK2 */
  return pclk2 * 2UL;                   /* APB2 分频   => 定时器时钟 = PCLK2 x 2 */
}

uint32_t PWM_GetArr(void)
{
  return (uint32_t)(htim1.Instance->ARR);
}

static void PWM_GpioInit(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  g.Pin   = PWM_GPIO_PIN;
  g.Mode  = GPIO_MODE_AF_PP;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Pull  = GPIO_NOPULL;
  HAL_GPIO_Init(PWM_GPIO_PORT, &g);
}

void PWM_Init(void)
{
  TIM_OC_InitTypeDef oc = {0};
  uint32_t arr;

  __HAL_RCC_TIM1_CLK_ENABLE();
  PWM_GpioInit();

  arr = (PWM_GetTimClk() / PWM_FREQ_HZ);
  if (arr < 2UL) arr = 2UL;
  arr -= 1UL;

  htim1.Instance               = TIM1;
  htim1.Init.Prescaler         = 0U;
  htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim1.Init.Period            = arr;
  htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0U;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK) Error_Handler();

  oc.OCMode     = TIM_OCMODE_PWM1;
  oc.Pulse      = 0U;
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, PWM_TIM_CHANNEL) != HAL_OK) Error_Handler();

  /* [!] MOE = 0 时,输出必须被【主动拉低】,不能悬空。
   *    默认 OSSI/OSSR 都是 0 => MOE=0 时引脚变高阻 => RT9624 的 PWM 输入悬空
   *    => 半桥可能误开 => 直通风险。
   *    OSSI=1 之后,MOE=0 时引脚被钉在"无效电平"(本工程 = 低)。
   *    死区仍为 0:死区由 RT9624 内部产生。
   * 注:ADC 用软件启动 + 连续扫描,不依赖 TRGO,所以这里不用配 MMS。 */
  {
    TIM_BreakDeadTimeConfigTypeDef bd = {0};
    bd.OffStateRunMode  = TIM_OSSR_ENABLE;
    bd.OffStateIDLEMode = TIM_OSSI_ENABLE;
    bd.LockLevel        = TIM_LOCKLEVEL_OFF;
    bd.DeadTime         = 0;
    bd.BreakState       = TIM_BREAK_DISABLE;
    bd.BreakPolarity    = TIM_BREAKPOLARITY_HIGH;
    bd.AutomaticOutput  = TIM_AUTOMATICOUTPUT_DISABLE;
    if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &bd) != HAL_OK) Error_Handler();
  }

  __HAL_TIM_MOE_DISABLE(&htim1);        /* 输出先关着,等 PWM_Enable() */

  /* 100kHz 更新中断 = 100kHz 控制中断的节拍源。
   * [!] 这里就要把计数器和中断打开(计数器一跑,控制中断就开始工作),
   *     AD 转换由 ADC_Init 里软件启动,与这里无关。
   *     MOE 仍然关着 => 栅极没有输出,安全。
   * 优先级 1(比 SysTick 高) */
  HAL_NVIC_SetPriority(TIM1_UP_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(TIM1_UP_IRQn);
  __HAL_TIM_ENABLE(&htim1);
  __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
}

/* 计数器与中断在 PWM_Init 里就已经在跑(ADC 的触发源),
 * 所以"使能输出"只差一个 MOE。 */
void PWM_Enable(void)
{
  __HAL_TIM_SET_COMPARE(&htim1, PWM_TIM_CHANNEL, 0U);
  __HAL_TIM_MOE_ENABLE(&htim1);
}

/* 关输出:只清 MOE(栅极回安全电平)。
 * 计数器和中断【保持不变】-- 控制环需要继续采样、继续监视。 */
void PWM_Disable(void)
{
  __HAL_TIM_SET_COMPARE(&htim1, PWM_TIM_CHANNEL, 0U);
  __HAL_TIM_MOE_DISABLE(&htim1);
  s_duty = 0.0f;
}

void PWM_SetDuty(float duty)
{
  uint32_t arr, ccr;

  if (duty < PWM_MIN_DUTY) duty = PWM_MIN_DUTY;
  if (duty > PWM_MAX_DUTY) duty = PWM_MAX_DUTY;

  arr = PWM_GetArr() + 1UL;
  ccr = (uint32_t)(duty * (float)arr);
  if (ccr > arr) ccr = arr;

  __HAL_TIM_SET_COMPARE(&htim1, PWM_TIM_CHANNEL, ccr);
  s_duty = duty;
}

float PWM_GetDuty(void)
{
  return s_duty;
}
