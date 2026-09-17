/**
 ******************************************************************************
 * @file    key.h
 * @brief   按键模块：5 个微动开关，支持短按 / 长按 / 长按后连按
 ******************************************************************************
 */
#ifndef __KEY_H
#define __KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/*============================== 用户配置区 ==============================*/
/* S1~S5 按键：接 GND，按下为低电平（内部上拉），请按实际原理图修改 */
#define KEY1_PORT       GPIOB
#define KEY1_PIN        GPIO_PIN_0
#define KEY1_ACTIVE_LOW 1       /* S1 电压 + */

#define KEY2_PORT       GPIOB
#define KEY2_PIN        GPIO_PIN_1
#define KEY2_ACTIVE_LOW 1       /* S2 电压 - */

#define KEY3_PORT       GPIOB
#define KEY3_PIN        GPIO_PIN_2
#define KEY3_ACTIVE_LOW 1       /* S3 开关机 */

#define KEY4_PORT       GPIOB
#define KEY4_PIN        GPIO_PIN_3
#define KEY4_ACTIVE_LOW 1       /* S4 模式切换（CV/CC） */

#define KEY5_PORT       GPIOB
#define KEY5_PIN        GPIO_PIN_4
#define KEY5_ACTIVE_LOW 1       /* S5 确认/保存 */

/* 说明：S4/S5 位于 PB3/PB4，这两个引脚默认被 JTAG 占用。
 * 开启下方宏后固件会关闭 JTAG（保留 SWD），PB3/PB4 变为普通 GPIO。
 * 若调试必须用 JTAG，请把 S4/S5 移到其他引脚并置 0。 */
#define KEY_NEED_JTAG_REMAP 1

/* 扫描与去抖参数（ms） */
#define KEY_SCAN_MS     5
#define KEY_DEBOUNCE_MS 20
#define KEY_LONG_MS     800      /* 长按阈值 */
#define KEY_REPEAT_MS   150      /* 长按后连按间隔 */
/*=======================================================================*/

/* 按键编号 */
#define KEY_ID_UP    0
#define KEY_ID_DOWN  1
#define KEY_ID_PWR   2
#define KEY_ID_MODE  3
#define KEY_ID_OK    4
#define KEY_COUNT    5

/* 事件类型 */
#define KEY_EVT_SHORT   0
#define KEY_EVT_LONG    1
#define KEY_EVT_REPEAT  2

void KEY_Init(void);
void KEY_Scan(void);        /* 主循环周期调用（内部 5ms 节流） */

/* 事件回调：默认空实现，在 main.c 中重写（去抖 + 长短按判定后触发） */
void Key_OnEvent(uint8_t key, uint8_t evt);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H */
