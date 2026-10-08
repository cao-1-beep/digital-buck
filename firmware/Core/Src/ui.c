/**
 ******************************************************************************
 * @file    ui.c
 * @brief   显示层实现(4 行状态页)
 ******************************************************************************
 */
#include "main.h"
#include "ui.h"
#include "OLED.h"

static uint32_t s_last = 0U;
static uint16_t s_lmt_hold = 0U;      /* 限流指示的保持计时(刷新周期数) */
static uint32_t s_lmt_last = 0U;

void UI_Init(void)
{
  OLED_Init();
  OLED_ColorTurn(0);                  /* 0 正常显示 */
  OLED_DisplayTurn(0);                /* 0 不翻转 */

  OLED_Clear();
  OLED_ShowString(0,  0, (uint8_t *)"DIGITAL BUCK", 16, 1);
  OLED_ShowString(0, 20, (uint8_t *)"12-25V -> 5-10V", 12, 1);
  OLED_ShowString(0, 36, (uint8_t *)"DBC-2  F103C8", 12, 1);
  OLED_ShowString(0, 52, (uint8_t *)"I2C PB6/PB7", 8, 1);
  OLED_Refresh();
  HAL_Delay(600);
}

/* 立刻显示一行大字 —— 给"已保存""故障"这类反馈用 */
void UI_Flash(const char *msg)
{
  OLED_Clear();
  OLED_ShowString(0, 24, (uint8_t *)msg, 16, 1);
  OLED_Refresh();
}

void UI_Update(void)
{
  uint32_t now = HAL_GetTick();
  const char *tag;

  if ((now - s_last) < UI_REFRESH_MS) return;
  s_last = now;

  /* 限流事件指示:检测到就亮 LMT,保持约 1 秒 */
  if (Control_GetLimitEvents() != s_lmt_last)
  {
    s_lmt_last  = Control_GetLimitEvents();
    s_lmt_hold  = (uint16_t)(1000U / UI_REFRESH_MS);
  }
  else if (s_lmt_hold > 0U)
  {
    s_lmt_hold--;
  }

  OLED_Clear();

  /* ---- 第 1 行:设定值 + 模式 / 故障名 ---- */
  OLED_ShowString(0, 0, (uint8_t *)"SET", 16, 1);
  OLED_Showdecimal(32, 0, Control_GetVset(), 1, 2, 16, 1);
  OLED_ShowString(64, 0, (uint8_t *)"V", 16, 1);

  if (g_fault != 0U)
  {
    tag = Protect_FaultStr();
    OLED_ShowString(80, 0, (uint8_t *)tag, 16, 1);   /* OCP/OTP/OVIN/... */
  }
  else if (Control_GetMode() == MODE_CC)
  {
    OLED_ShowString(80, 0, (uint8_t *)"CC", 16, 1);
  }
  else
  {
    OLED_ShowString(80, 0, (uint8_t *)"CV", 16, 1);
  }

  /* ---- 第 2 行:输出电压 ---- */
  OLED_ShowString(0, 16, (uint8_t *)"OUT", 16, 1);
  OLED_Showdecimal(32, 16, ADC_GetVout(), 2, 2, 16, 1);
  OLED_ShowString(72, 16, (uint8_t *)"V", 16, 1);

  /* ---- 第 3 行:输出电流 + 限流指示 ---- */
  OLED_ShowString(0, 32, (uint8_t *)"IO", 16, 1);
  OLED_Showdecimal(32, 32, ADC_GetIout(), 2, 2, 16, 1);
  OLED_ShowString(64, 32, (uint8_t *)"A", 16, 1);
  if (s_lmt_hold > 0U)
  {
    OLED_ShowString(88, 32, (uint8_t *)"LMT", 16, 1);
  }

  /* ---- 第 4 行:输入电压 + 温度 ---- */
  OLED_ShowString(0, 48, (uint8_t *)"IN", 16, 1);
  OLED_Showdecimal(32, 48, ADC_GetVin(), 2, 1, 16, 1);
  OLED_ShowString(64, 48, (uint8_t *)"V", 16, 1);
  {
    float t = ADC_GetTempC();
    if (t > -50.0f)
    {
      OLED_ShowNum(88, 48, (uint32_t)(t + 0.5f), 2, 16, 1);
      OLED_ShowString(112, 48, (uint8_t *)"C", 16, 1);
    }
    else
    {
      OLED_ShowString(88, 48, (uint8_t *)"--C", 16, 1);
    }
  }

  OLED_Refresh();
}
