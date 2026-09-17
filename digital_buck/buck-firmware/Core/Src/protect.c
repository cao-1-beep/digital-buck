/**
 ******************************************************************************
 * @file    protect.c
 * @brief   故障检测：过流（OCP）/ 过压（OVP）/ 欠压（UVP）+ 可选外部比较器
 *
 * 逻辑：
 *  - 所有检测在 100kHz 控制环内完成（Protect_Check），带计数去抖；
 *  - 故障一旦锁定（g_fault 置位）→ Control_Disable() 立即关断 PWM；
 *  - 故障锁存需用户清除（S3 长按 / clear 命令 / enable 命令会自动清）；
 *  - UVP 例外：输入电压恢复后自动清除该位（输出仍需手动使能）。
 ******************************************************************************
 */
#include "main.h"

volatile uint8_t g_fault = 0;

static float  s_ocp = STORAGE_DEFAULT_OCP;
static uint8_t s_ocp_cnt = 0, s_ovp_cnt = 0, s_uvp_cnt = 0;

#ifdef PROTECT_USE_EXT_COMP
static void Protect_ExtNvic(uint8_t idx);   /* 前向声明 */
#endif

/* 触发并锁存故障（只执行一次，避免反复调用 Control_Disable） */
static void Protect_Latch(uint8_t bit)
{
  if ((g_fault & bit) == 0) {
    g_fault |= bit;
    Control_Disable();
  }
}

void Protect_Init(void)
{
  g_fault = 0;
  s_ocp = STORAGE_DEFAULT_OCP;
  s_ocp_cnt = 0;
  s_ovp_cnt = 0;
  s_uvp_cnt = 0;

#ifdef PROTECT_USE_EXT_COMP
  {
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_AFIO_CLK_ENABLE();   /* F1 的 EXTI 需要 AFIO 时钟 */
    g.Mode = PROTECT_EXT_MODE;
    g.Pull = GPIO_PULLUP;
    g.Pin  = OCP_CMP_PIN;
    HAL_GPIO_Init(OCP_CMP_PORT, &g);
    g.Pin  = OVP_CMP_PIN;
    HAL_GPIO_Init(OVP_CMP_PORT, &g);
    Protect_ExtNvic(OCP_CMP_PIN_IDX);
    Protect_ExtNvic(OVP_CMP_PIN_IDX);
  }
#endif
}

/* 每个控制周期调用：vout/iout/vin 为最新采样 */
void Protect_Check(float vout, float iout, float vin)
{
  float ovp_lim;

  /* 过流：Iout > OCP 阈值（默认 3.5A，可 set ocp 调整） */
  if (iout > s_ocp) {
    if (++s_ocp_cnt >= OCP_DEBOUNCE) Protect_Latch(FAULT_OCP);
  } else {
    s_ocp_cnt = 0;
  }

  /* 过压：动态阈值 VSET×1.1 与绝对上限 10.8V 取较严者 */
  ovp_lim = g_vset * (1.0f + OVP_RATIO);
  if (ovp_lim < OVP_ABS_MIN) ovp_lim = OVP_ABS_MIN;
  if (vout > ovp_lim || vout > OVP_ABS_MAX) {
    if (++s_ovp_cnt >= OVP_DEBOUNCE) Protect_Latch(FAULT_OVP);
  } else {
    s_ovp_cnt = 0;
  }

#if ENABLE_UVP
  /* 欠压：输入低于 10V 锁断；恢复后自动清除该位 */
  if (vin < UVP_THRESHOLD) {
    if (++s_uvp_cnt >= UVP_DEBOUNCE) Protect_Latch(FAULT_UVP);
  } else {
    s_uvp_cnt = 0;
    if ((g_fault & FAULT_UVP) && vin > (UVP_THRESHOLD + UVP_HYST))
      g_fault &= (uint8_t)~FAULT_UVP;
  }
#endif
}

void Protect_ClearFaults(void)
{
  g_fault = 0;
}

void Protect_SetOcp(float a)
{
  if (a < 0.5f) a = 0.5f;
  if (a > 6.0f) a = 6.0f;
  s_ocp = a;
}

float Protect_GetOcp(void)
{
  return s_ocp;
}

/* ================== 可选：外部比较器 EXTI 处理 ================== */
#ifdef PROTECT_USE_EXT_COMP

static void Protect_ExtNvic(uint8_t idx)
{
  IRQn_Type irq;

  switch (idx) {
    case 0:  irq = EXTI0_IRQn;  break;
    case 1:  irq = EXTI1_IRQn;  break;
    case 2:  irq = EXTI2_IRQn;  break;
    case 3:  irq = EXTI3_IRQn;  break;
    default: irq = EXTI4_IRQn;  break;
  }
  HAL_NVIC_SetPriority(irq, 2, 0);
  HAL_NVIC_EnableIRQ(irq);
}

/* 覆盖 HAL 弱回调（stm32f1xx_hal_gpio.c） */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == OCP_CMP_PIN)      Protect_Latch(FAULT_EXT_OC);
  else if (GPIO_Pin == OVP_CMP_PIN) Protect_Latch(FAULT_EXT_OV);
}

#endif /* PROTECT_USE_EXT_COMP */
