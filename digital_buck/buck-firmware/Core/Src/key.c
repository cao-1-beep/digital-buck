/**
 ******************************************************************************
 * @file    key.c
 * @brief   按键扫描状态机：消抖（20ms）→ 短按 / 长按（800ms）/ 连按（150ms）
 *
 * 状态机：
 *   空闲(0) --按下--> 预按下(1) --持续>=消抖时间--> 按下(2)
 *   预按下(1) --提前释放--> 空闲(0)            （抖动忽略）
 *   按下(2) --释放且未到长按阈值--> 短按事件
 *   按下(2) --持续>=长按阈值--> 长按事件（仅一次）
 *   长按后每 KEY_REPEAT_MS 触发一次连按事件
 ******************************************************************************
 */
#include "main.h"

typedef struct {
  GPIO_TypeDef *port;
  uint16_t pin;
  uint8_t  active_low;
} KeyCfg_t;

static const KeyCfg_t s_key_cfg[KEY_COUNT] = {
  { KEY1_PORT, KEY1_PIN, KEY1_ACTIVE_LOW },
  { KEY2_PORT, KEY2_PIN, KEY2_ACTIVE_LOW },
  { KEY3_PORT, KEY3_PIN, KEY3_ACTIVE_LOW },
  { KEY4_PORT, KEY4_PIN, KEY4_ACTIVE_LOW },
  { KEY5_PORT, KEY5_PIN, KEY5_ACTIVE_LOW },
};

typedef struct {
  uint8_t  state;          /* 0 空闲 / 1 预按下 / 2 按下 */
  uint32_t t_press;        /* 按下时刻 */
  uint32_t t_last_rep;     /* 上次连按时刻 */
  uint8_t  evt_fired_long; /* 长按事件是否已触发 */
} KeyState_t;

static KeyState_t s_key[KEY_COUNT];

static uint8_t key_is_pressed(uint8_t id)
{
  GPIO_PinState lvl = HAL_GPIO_ReadPin(s_key_cfg[id].port, s_key_cfg[id].pin);
  return (lvl == (s_key_cfg[id].active_low ? GPIO_PIN_RESET : GPIO_PIN_SET));
}

/* 默认事件处理（弱符号），main.c 中提供强定义覆盖 */
__weak void Key_OnEvent(uint8_t key, uint8_t evt)
{
  (void)key;
  (void)evt;
}

void KEY_Init(void)
{
  GPIO_InitTypeDef g = {0};
  uint8_t i;

  __HAL_RCC_GPIOB_CLK_ENABLE();

#if KEY_NEED_JTAG_REMAP
  /* 关闭 JTAG（SWJ_CFG=010，仅保留 SWD），释放 PB3/PB4 作 GPIO */
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_AFIO_REMAP_SWJ_NOJTAG();
#endif

  g.Mode  = GPIO_MODE_INPUT;
  g.Pull  = GPIO_PULLUP;      /* 默认高电平，按下拉低 */
  for (i = 0; i < KEY_COUNT; i++) {
    g.Pin = s_key_cfg[i].pin;
    HAL_GPIO_Init(s_key_cfg[i].port, &g);
    s_key[i].state = 0;
  }
}

void KEY_Scan(void)
{
  static uint32_t s_last = 0;
  uint32_t now = HAL_GetTick();
  uint8_t i;

  if (now - s_last < KEY_SCAN_MS) return;   /* 节流：每 5ms 扫一次 */
  s_last = now;

  for (i = 0; i < KEY_COUNT; i++) {
    uint8_t pr = key_is_pressed(i);
    KeyState_t *k = &s_key[i];

    switch (k->state) {
      case 0:                             /* 空闲 */
        if (pr) {
          k->t_press = now;
          k->state = 1;
        }
        break;

      case 1:                             /* 预按下（消抖中） */
        if (!pr) {                        /* 抖动，复位 */
          k->state = 0;
          break;
        }
        if (now - k->t_press >= KEY_DEBOUNCE_MS) {
          k->state = 2;
          k->evt_fired_long = 0;
          k->t_last_rep = now;
        }
        break;

      case 2:                             /* 已确认按下 */
        if (!pr) {                        /* 释放 */
          k->state = 0;
          if (!k->evt_fired_long)
            Key_OnEvent(i, KEY_EVT_SHORT);  /* 未到长按 → 短按 */
          break;
        }
        if (!k->evt_fired_long && now - k->t_press >= KEY_LONG_MS) {
          k->evt_fired_long = 1;
          k->t_last_rep = now;          /* 复位连按计时，防止本拍误触发 REPEAT */
          Key_OnEvent(i, KEY_EVT_LONG); /* 长按（一次） */
        }
        if (k->evt_fired_long && now - k->t_last_rep >= KEY_REPEAT_MS) {
          k->t_last_rep = now;
          Key_OnEvent(i, KEY_EVT_REPEAT); /* 长按后连按 */
        }
        break;
    }
  }
}
