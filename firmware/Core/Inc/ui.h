/**
 ******************************************************************************
 * @file    ui.h
 * @brief   显示层:把控制状态画到 I2C OLED(PB6=SCL / PB7=SDA)
 *
 * 本板没有串口(PA9 悬空、PA10 被 PWM 占用),OLED 是【唯一】的人机界面。
 * 底层驱动用 basp\OLED.c(SSD1315/SSD1306 兼容),这里只做排版。
 *
 * 4 行 x 16 字符(size 16 字体,每字符 8x16):
 *   SET 5.00V   CV|CC|故障名
 *   OUT 5.02V
 *   IO  1.25A   LMT
 *   IN 15.2V    42C
 ******************************************************************************
 */
#ifndef __UI_H
#define __UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* 刷新周期(ms)。整屏重画一帧约 25ms(软件 I2C @ 本工程节拍),
 * 5Hz 时约占 CPU 10% 左右;要更省可以调大。 */
#define UI_REFRESH_MS   200U

void UI_Init(void);                       /* OLED 初始化 + 开机画面 */
void UI_Update(void);                     /* 主循环调用(内部按周期节流) */
void UI_Flash(const char *msg);           /* 立刻显示一行大字(如 "SAVED") */
void UI_InitFastHint(void);               /* 保留 */

#ifdef __cplusplus
}
#endif

#endif /* __UI_H */
