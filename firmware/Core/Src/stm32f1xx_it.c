/**
 ******************************************************************************
 * @file    stm32f1xx_it.c
 * @brief   中断服务函数
 *
 * 本工程只有两个中断:
 *   TIM1_UP  (优先级 1) :100kHz 控制中断
 *     TIM1_UP_IRQHandler -> HAL_TIM_IRQHandler
 *       -> HAL_TIM_PeriodElapsedCallback(定义在 pid.c)
 *       -> ADC_OnPwmTick() + Control_OnSample():电流更新 + 逐周期限流 + 分频 PID
 *   SysTick  (优先级 15):HAL_IncTick,给 HAL_GetTick 与主循环节拍
 *
 * [!] 没有 USART1 中断:本板 PA9 悬空、PA10 被 PWM 占用,串口不可用。
 * [!] 没有 DMA1_Channel1 中断:ADC 的 DMA 是环形模式,
 *     控制中断直接读缓冲,不需要 DMA 传输完成中断。
 * [!] HAL_TIM_PeriodElapsedCallback 只允许有一处定义(在 pid.c),
 *     不要在本文件或 main.c 里再定义一次。
 ******************************************************************************
 */
#include "main.h"
#include "stm32f1xx_it.h"

/******************************************************************************/
/*                  Cortex-M3 内核异常                                        */
/******************************************************************************/
void NMI_Handler(void)
{
  while (1) { }
}

/* 硬件异常:死循环(调试时可在此打断点看 SCB->CFSR / HFSR) */
void HardFault_Handler(void)
{
  while (1) { }
}

void MemManage_Handler(void)
{
  while (1) { }
}

void BusFault_Handler(void)
{
  while (1) { }
}

void UsageFault_Handler(void)
{
  while (1) { }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

/******************************************************************************/
/*                  外设中断                                                  */
/******************************************************************************/
/* 100kHz 控制中断:真正的活儿在 HAL_TIM_PeriodElapsedCallback(pid.c)里 */
void TIM1_UP_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim1);
}
