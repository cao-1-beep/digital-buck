/**
 ******************************************************************************
 * @file    key.h
 * @brief   按键模块:4 个功能键(短按 / 长按 / 长按后连按)
 *
 * [!] 与代码原版的差别(按原理图 + PCB 网表改正):
 *   原版:PB0~PB4 五个键,还为此关掉 JTAG 腾出 PB3/PB4。
 *   实际:S1 -> PA1、S2 -> PA2、S3 -> PA3、S5 -> PA0(另一端 GND);
 *         S4 接的是【NRST】= 复位键,固件里不存在。
 *         板上已有 10K 上拉到 +3.3V + 100nF 到地。
 *   顺带:不再关 JTAG ==> 保住 SWO(PB3)的可能(将来想用 ITM printf 不用改这里)。
 ******************************************************************************
 */
#ifndef __KEY_H
#define __KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/*============================== 用户配置区 ==============================*/
#define KEY1_PORT       GPIOA
#define KEY1_PIN        GPIO_PIN_1    /* S1 电压 + */
#define KEY1_ACTIVE_LOW 1
#define KEY2_PORT       GPIOA
#define KEY2_PIN        GPIO_PIN_2    /* S2 电压 - */
#define KEY2_ACTIVE_LOW 1
#define KEY3_PORT       GPIOA
#define KEY3_PIN        GPIO_PIN_3    /* S3 开关机 */
#define KEY3_ACTIVE_LOW 1
#define KEY4_PORT       GPIOA
#define KEY4_PIN        GPIO_PIN_0    /* S5 保存参数(S4 是复位键) */
#define KEY4_ACTIVE_LOW 1

#define KEY_COUNT       4

/* 扫描与去抖参数(ms) */
#define KEY_SCAN_MS     5
#define KEY_DEBOUNCE_MS 20
#define KEY_LONG_MS     800       /* 长按阈值 */
#define KEY_REPEAT_MS   150       /* 长按后连按间隔 */
/*=======================================================================*/

/* 按键编号 */
#define KEY_ID_UP    0
#define KEY_ID_DOWN  1
#define KEY_ID_PWR   2
#define KEY_ID_OK    3

/* 事件类型 */
#define KEY_EVT_SHORT   0
#define KEY_EVT_LONG    1
#define KEY_EVT_REPEAT  2

void KEY_Init(void);
void KEY_Scan(void);        /* 主循环周期调用(内部 5ms 节流) */

/* 事件回调:默认弱定义空实现,在 main.c 里强定义覆盖 */
void Key_OnEvent(uint8_t key, uint8_t evt);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H */
