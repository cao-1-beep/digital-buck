/**
 ******************************************************************************
 * @file    pwm.c
 * @brief   TIM1 互补 PWM 输出（PA8 上管 / PB13 下管），死区 ~100ns
 *
 * 说明：
 *  - 开关频率 100kHz（PWM_FREQ_HZ），ARR 按实际 PCLK2 动态计算，避免硬编码；
 *  - 死区寄存器 DTG = 死区时间 × 定时器时钟 = 100ns × 72MHz = 7.2 → 7；
 *  - 定时器始终运行（更新中断 = 控制周期），输出通断仅通过 MOE 位控制，
 *    使能/关闭输出不打断控制环；
 *  - 关闭输出时 MOE=0，在 OSSI=0 配置下两路输出均为低电平（MOSFET 全关断）。
 ******************************************************************************
 */
#include "main.h"

TIM_HandleTypeDef htim1;

/* 中断（写）与主循环（读）共享，必须 volatile */
static volatile float s_duty = 0.0f;
static uint32_t s_arr  = 0;

void PWM_Init(void)
{
  TIM_OC_InitTypeDef             oc  = {0};
  TIM_BreakDeadTimeConfigTypeDef bdt = {0};
  GPIO_InitTypeDef               g   = {0};
  uint32_t tim_clk;
  uint32_t dtg;

  /* 1. 时钟使能 */
  __HAL_RCC_TIM1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* 2. GPIO：PA8 / PB13 复用推挽输出 */
  g.Pin   = PWM_HIGH_PIN;
  g.Mode  = GPIO_MODE_AF_PP;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(PWM_HIGH_PORT, &g);
  g.Pin   = PWM_LOW_PIN;
  HAL_GPIO_Init(PWM_LOW_PORT, &g);

  /* 3. 按实际 PCLK2 计算 ARR 与死区 DTG（不依赖硬编码的 72MHz） */
  tim_clk = HAL_RCC_GetPCLK2Freq();                    /* 默认 72MHz */
  s_arr   = tim_clk / PWM_FREQ_HZ - 1UL;               /* 719 @100kHz */
  dtg     = (uint32_t)((float)PWM_DEADTIME_NS *
                       (float)tim_clk / 1000000000.0f); /* 100ns → 7 */

  /* 4. TIM1 基本配置（向上计数，边沿对齐） */
  htim1.Instance               = TIM1;
  htim1.Init.Prescaler         = 0;
  htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim1.Init.Period            = s_arr;
  htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK) Error_Handler();

  /* 5. 通道 1：PWM 模式 1（CCR < CNT 时输出有效），占空比初值 0 */
  oc.OCMode       = TIM_OCMODE_PWM1;
  oc.Pulse        = 0;
  oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
  oc.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
  oc.OCFastMode   = TIM_OCFAST_DISABLE;
  oc.OCIdleState  = TIM_OCIDLESTATE_RESET;    /* 空闲/关断时输出低电平 */
  oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();

  /* 6. 死区：DTG=7 → 7×13.89ns ≈ 97.2ns ≈ 100ns */
  bdt.OffStateRunMode  = TIM_OSSR_DISABLE;
  bdt.OffStateIDLEMode = TIM_OSSI_DISABLE;
  bdt.LockLevel        = TIM_LOCKLEVEL_OFF;
  bdt.DeadTime         = dtg;
  bdt.BreakState       = TIM_BREAK_DISABLE;
  bdt.BreakPolarity    = TIM_BREAKPOLARITY_HIGH;
  bdt.BreakFilter      = 0;
  bdt.AutomaticOutput  = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &bdt) != HAL_OK) Error_Handler();

  /* 7. 更新中断 = 控制周期（100kHz），优先级 1（高于串口 2） */
  HAL_NVIC_SetPriority(TIM1_UP_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(TIM1_UP_IRQn);
  if (HAL_TIM_Base_Start_IT(&htim1) != HAL_OK) Error_Handler();

  /* 8. 默认关闭输出 */
  PWM_Disable();
  PWM_SetDuty(0.0f);
}

/* 使能输出：置位通道使能位并打开主输出 MOE */
void PWM_Enable(void)
{
  TIM1->CCER |= (TIM_CCER_CC1E | TIM_CCER_CC1NE);
  TIM1->BDTR |= TIM_BDTR_MOE;
}

/* 关闭输出：清除 MOE。OSS 空闲电平配置为低，两管立即关断（不破坏 CCER） */
void PWM_Disable(void)
{
  TIM1->BDTR &= ~TIM_BDTR_MOE;
}

/* 设置占空比（0~PWM_MAX_DUTY）。CCR 预装载使能，下一周期生效 */
void PWM_SetDuty(float duty)
{
  if (duty < 0.0f)            duty = 0.0f;
  if (duty > PWM_MAX_DUTY)    duty = PWM_MAX_DUTY;
  s_duty = duty;
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1,
                        (uint32_t)(duty * (float)s_arr));
}

float PWM_GetDuty(void)
{
  return s_duty;
}

uint32_t PWM_GetArr(void)
{
  return s_arr;
}
