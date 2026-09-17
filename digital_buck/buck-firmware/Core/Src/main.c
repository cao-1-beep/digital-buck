/**
 ******************************************************************************
 * @file    main.c
 * @brief   数字降压电源主程序
 *
 * 系统结构：
 *   ┌────────┐  10kHz   ┌────────┐  100kHz  ┌────────┐
 *   │ 电压环 │→Icmd→    │ 电流环 │→Duty→   │ TIM1   │→ 半桥 MOSFET
 *   │ PID    │          │ PID    │          │ PWM    │
 *   └───┬────┘          └───┬────┘          └────────┘
 *       ↑ Vout              ↑ Iout
 *       └──────── ADC1 DMA 环形缓冲（3 通道连续扫描）─────────┘
 *
 * 主循环任务：
 *   - 按键扫描（KEY_Scan，内部 5ms 节流）
 *   - 串口命令处理（UART_Poll）
 *   - 状态 LED、故障上报、周期遥测
 *
 * 中断任务（TIM1 更新，100kHz）：PID_ControlTick()，见 pid.c。
 ******************************************************************************
 */
#include "main.h"

/* 故障名称表（用于串口上报） */
static const char *Fault_Names(uint8_t f);

static void LED_Update(void);
static void PrintStatus(void);

int main(void)
{
  uint32_t s_last_fault = 0;
  uint32_t s_tel_last   = 0;

  HAL_Init();
  SystemClock_Config();

  /* 状态 LED */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  {
    GPIO_InitTypeDef g = {0};
    g.Pin   = LED_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &g);
  }
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, LED_OFF_LEVEL);

  /* 初始化顺序：串口（调试输出）→ ADC → 控制环 → 保护 → 参数 → PWM → 按键
   * 先加载 Flash 参数再启动 PWM 中断，保证 100kHz 控制环启动时参数即已就位 */
  UART_Init();
  uart_Puts("\r\n=== Digital Buck Converter DBC-1 v1.0 ===\r\n");

  ADC_Init();
  PID_ControlInit();
  Protect_Init();
  Storage_LoadAndApply();   /* 从 Flash 读取上次保存的参数 */
  PWM_Init();               /* 启动 TIM1，100kHz 控制中断开始运行 */
  KEY_Init();

  Control_Disable();        /* 安全起见：上电默认关闭输出 */
  uart_Puts("Ready. type 'help' for commands.\r\n");

  for (;;) {
    uint32_t now = HAL_GetTick();

    KEY_Scan();
    UART_Poll();
    LED_Update();

    /* 故障边沿上报（锁存变化时打印一次） */
    if (g_fault != s_last_fault) {
      if (g_fault != 0)
        uart_Printf("FAULT 0x%x (%s) output disabled\r\n",
                    (unsigned int)g_fault, Fault_Names(g_fault));
      s_last_fault = g_fault;
    }

#if UART_TELEMETRY_MS > 0
    /* 周期遥测 */
    if (now - s_tel_last >= UART_TELEMETRY_MS) {
      s_tel_last = now;
      PrintStatus();
    }
#endif
  }
}

/* ============================ 控制周期中断 ============================ */
/* TIM1 更新事件回调（100kHz）→ 双闭环控制（pid.c） */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM1) {
    PID_ControlTick();
  }
}

/* ============================ 按键事件 ============================ */
void Key_OnEvent(uint8_t key, uint8_t evt)
{
  if (evt == KEY_EVT_SHORT) {
    switch (key) {
      case KEY_ID_UP:    /* S1：电压 +0.1V */
        Control_SetVset(Control_GetVset() + 0.1f);
        break;
      case KEY_ID_DOWN:  /* S2：电压 -0.1V */
        Control_SetVset(Control_GetVset() - 0.1f);
        break;
      case KEY_ID_PWR:   /* S3：开关机 */
        if (Control_IsEnabled()) Control_Disable();
        else                    Control_Enable();
        break;
      case KEY_ID_MODE:  /* S4：CV/CC 切换 */
        Control_SetMode(Control_GetMode() == MODE_CC ? MODE_CV : MODE_CC);
        uart_Printf("OK mode=%s\r\n", Control_GetMode() == MODE_CC ? "CC" : "CV");
        break;
      case KEY_ID_OK:    /* S5：保存参数到 Flash */
        if (Storage_SaveCurrent() == 0) uart_Printf("OK saved to flash\r\n");
        else                             uart_Printf("ERROR flash write failed\r\n");
        break;
    }
  } else if (evt == KEY_EVT_LONG) {
    switch (key) {
      case KEY_ID_UP:    /* S1 长按：+0.5V */
        Control_SetVset(Control_GetVset() + 0.5f);
        break;
      case KEY_ID_DOWN:  /* S2 长按：-0.5V */
        Control_SetVset(Control_GetVset() - 0.5f);
        break;
      case KEY_ID_PWR:   /* S3 长按：清除故障 */
        Protect_ClearFaults();
        uart_Printf("OK faults cleared\r\n");
        break;
      case KEY_ID_MODE:  /* S4 长按：打印状态 */
        PrintStatus();
        break;
      case KEY_ID_OK:    /* S5 长按：恢复出厂默认（RAM 中生效，短按 S5 保存） */
        Control_SetVset(STORAGE_DEFAULT_VSET);
        Control_SetIlimit(STORAGE_DEFAULT_ILIMIT);
        Protect_SetOcp(STORAGE_DEFAULT_OCP);
        Control_SetMode(STORAGE_DEFAULT_MODE);
        Control_SetVpid(STORAGE_DEFAULT_VKP, STORAGE_DEFAULT_VKI, STORAGE_DEFAULT_VKD);
        Control_SetIpid(STORAGE_DEFAULT_IKP, STORAGE_DEFAULT_IKI, STORAGE_DEFAULT_IKD);
        uart_Printf("OK defaults restored (press S5 short to save)\r\n");
        break;
    }
  } else if (evt == KEY_EVT_REPEAT) {
    /* 长按后连按：S1/S2 继续 ±0.1V */
    if (key == KEY_ID_UP)   Control_SetVset(Control_GetVset() + 0.1f);
    if (key == KEY_ID_DOWN) Control_SetVset(Control_GetVset() - 0.1f);
  }
}

/* ============================ 辅助函数 ============================ */

static void LED_Update(void)
{
  GPIO_PinState lvl;
  uint32_t now = HAL_GetTick();

  if (g_fault != 0) {
    lvl = ((now / 250U) & 1U) ? LED_ON_LEVEL : LED_OFF_LEVEL;   /* 故障：2Hz 闪烁 */
  } else if (Control_IsEnabled()) {
    lvl = LED_ON_LEVEL;                                          /* 开启：常亮 */
  } else {
    lvl = LED_OFF_LEVEL;                                         /* 关闭：熄灭 */
  }
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, lvl);
}

static void PrintStatus(void)
{
  uart_Printf("T vout=%.3f iout=%.3f vin=%.2f duty=%.1f%% mode=%s on=%d fault=0x%x\r\n",
              ADC_GetVout(), ADC_GetIout(), ADC_GetVin(),
              PWM_GetDuty() * 100.0f,
              Control_GetMode() == MODE_CC ? "CC" : "CV",
              (int)Control_IsEnabled(), (unsigned int)g_fault);
}

static const char *Fault_Names(uint8_t f)
{
  static const char *names[5] = { "OCP", "OVP", "UVP", "EXT-OC", "EXT-OV" };
  static const uint8_t bits[5] = { FAULT_OCP, FAULT_OVP, FAULT_UVP,
                                   FAULT_EXT_OC, FAULT_EXT_OV };
  static char buf[48];
  uint8_t i, pos = 0, first = 1;

  buf[0] = '\0';
  for (i = 0; i < 5; i++) {
    if (f & bits[i]) {
      uint8_t j = 0;
      if (!first && pos < sizeof(buf) - 1) buf[pos++] = ' ';
      first = 0;
      while (names[i][j] != '\0' && pos < sizeof(buf) - 1)
        buf[pos++] = names[i][j++];
    }
  }
  if (buf[0] == '\0') { buf[0] = '-'; buf[1] = '\0'; }
  return buf;
}

/* ============================ 时钟配置 ============================ */
/* HSE 8MHz × PLL×9 = 72MHz；HSE 起振失败自动回退 HSI/2×16 = 64MHz
 * （HSI 路径 F103 最高只能到 64MHz，ARR 按实际时钟动态计算，无需改代码） */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    /* 回退：HSI 8MHz/2 × 16 = 64MHz */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState       = RCC_HSI_ON;
    RCC_OscInitStruct.HSEState       = RCC_HSE_OFF;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSI_DIV2;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL16;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();
  }

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;   /* 36MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;   /* 72MHz */
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    Error_Handler();
}

/* ============================ 错误处理 ============================ */
void Error_Handler(void)
{
  __disable_irq();
  for (;;) {
    volatile uint32_t i;
    for (i = 0; i < 200000UL; i++) { }
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
  }
}
