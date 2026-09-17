/**
 ******************************************************************************
 * @file    stm32f1xx_it.c
 * @brief   中断服务函数
 *
 * 说明：
 *  - TIM1_UP：控制周期中断（100kHz），转交 HAL 后回调到 main.c 的
 *    HAL_TIM_PeriodElapsedCallback → PID_ControlTick()；
 *  - USART1：自定义接收中断（环形缓冲），不使用 HAL_UART_IRQHandler，
 *    避免其"未处于接收状态时关闭 RXNE"的行为；
 *  - EXTI0~4：外部比较器保护（可选），默认未使用，无副作用。
 *
 * 若使用 CubeMX 生成工程：删除其生成的 stm32f1xx_it.c，或把本文件中的
 * TIM1_UP_IRQHandler / USART1_IRQHandler 复制进 CubeMX 的 it.c 中。
 ******************************************************************************
 */
#include "stm32f1xx_it.h"
#include "main.h"

void NMI_Handler(void) { }

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

void SVC_Handler(void) { }
void DebugMon_Handler(void) { }
void PendSV_Handler(void) { }

void SysTick_Handler(void)
{
  HAL_IncTick();
}

/* 控制周期：PWM 更新事件（100kHz） */
void TIM1_UP_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim1);
}

/* 串口接收（自定义环形缓冲） */
void USART1_IRQHandler(void)
{
  UART_RxISR();
}

/* 可选外部比较器保护（EXTI 线 0~4） */
void EXTI0_IRQHandler(void)       { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0); }
void EXTI1_IRQHandler(void)       { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1); }
void EXTI2_IRQHandler(void)       { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2); }
void EXTI3_IRQHandler(void)       { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3); }
void EXTI4_IRQHandler(void)       { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4); }
void EXTI9_5_IRQHandler(void)     { }
void EXTI15_10_IRQHandler(void)   { }

/* ADC DMA 为环形模式，无需中断 */
void DMA1_Channel1_IRQHandler(void) { }
