/**
 ******************************************************************************
 * @file    uart.h
 * @brief   串口模块：USART1 中断接收（环形缓冲）+ 文本命令解析
 ******************************************************************************
 */
#ifndef __UART_H
#define __UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/*============================== 用户配置区 ==============================*/
/* 波特率：115200 8N1 */
#define UART_BAUDRATE   115200UL
/*=======================================================================*/

extern UART_HandleTypeDef huart1;

void UART_Init(void);                  /* 初始化 USART1 + RX 中断 */
void UART_Poll(void);                  /* 主循环调用：取行并解析命令 */
void UART_RxISR(void);                 /* 接收中断服务（it.c 中调用） */
void uart_Printf(const char *fmt, ...);/* 轻量格式化输出：%s %c %d %u %x %f %%.Nf */
void uart_Puts(const char *s);         /* 发送字符串 */

#ifdef __cplusplus
}
#endif

#endif /* __UART_H */
