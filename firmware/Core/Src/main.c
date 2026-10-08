/**
 ******************************************************************************
 * @file    main.c
 * @brief   数字降压电源主程序
 *
 * 系统结构:
 *   电压外环 PID(5kHz) -> Icmd -> 电流内环 PID(50kHz) -> Duty
 *     -> TIM1_CH3(PA10) -> RT9624 -> 半桥(Q1/Q2) -> L1(68uH) -> Vout
 *   反馈:ADC1 四通道(TIM1 TRGO 触发 + DMA 环形)
 *     PA6 = 输出电流(U2 = LMV321,增益 15.7,偏置 0.3765V,R11 = 30mR)
 *     PA5 = 输入电压(1:11)   PA7 = 输出电压(1:2,R20 已改 3.3K)
 *     PA4 = 外部 NTC(MF52A103F3950,R30 = 68R)
 *
 * 主循环任务:
 *   KEY_Scan()          按键(内部 5ms 节流)
 *   Task1ms()           1ms:Vin/Vout/NTC 换算 -> 降额与钳位 -> 慢保护
 *   UI_Update()         OLED 状态页(内部 200ms 节流)
 *
 * [!] 本工程【不是 CubeMX 生成的】,没有 USER CODE 块。
 * [!] 本板没有串口(PA9 悬空、PA10 被 PWM 占用) => uart.c 不参与编译。
 * [!] 上电默认【关闭输出】;要输出必须按 S3(开关机)。
 ******************************************************************************
 */
#include "main.h"
#include "ui.h"

static void Task1ms(void);

int main(void)
{
  uint32_t last_1ms = 0U;
  uint32_t now;

  HAL_Init();

  /* 时钟:HSE 8MHz(原理图 Y1 的 Value = 8M)x PLL9 = 72MHz */
  SystemClock_Config();

  /* ---- 初始化顺序很重要,别调换 ----
   * 1) PWM_Init:TIM1 计数器 + 100kHz 中断先跑起来,它是 ADC 的触发源(TRGO)。
   *             MOE 仍然关着 => 栅极没有输出,安全。
   * 2) ADC_Init:启动 DMA + 做电流零点标定。
   *             此刻输出必然关断 => 标定到的就是真正的零点。 */
  PWM_Init();
  ADC_Init();

  PID_ControlInit();
  Protect_Init();
  Storage_LoadAndApply();     /* 从 Flash 读参数并应用(无效则用出厂默认) */
  KEY_Init();
  UI_Init();

  Control_Disable();          /* 再确认一次:上电默认关输出 */

  while (1)
  {
    KEY_Scan();

    now = HAL_GetTick();
    if (now != last_1ms)
    {
      last_1ms = now;
      Task1ms();
    }

    UI_Update();
  }
}

/*==================== 1ms 软节拍 ====================*/
static void Task1ms(void)
{
  ADC_UpdateSlow();          /* Vin / Vout / NTC 换算与平滑(NTC 内部再降速) */
  Control_Tick1ms();         /* 过温降额、Vset 钳位、欠压/过压/过温检查 */
  Storage_NoteTemp(ADC_GetTempC());   /* 记录历史最高温 */
}

/*==================== 按键事件(强定义,覆盖 key.c 里的弱符号)====================
 * 映射(按原理图):PA1 = S1 电压+ / PA2 = S2 电压- / PA3 = S3 开关机 / PA0 = S5 保存
 * 注:S4 接的是 NRST,是复位键,固件里不存在。
 */
void Key_OnEvent(uint8_t key, uint8_t evt)
{
  float v;

  switch (key)
  {
    /* ---- 电压 + ---- */
    case KEY_ID_UP:
      v = Control_GetVset();
      v += (evt == KEY_EVT_LONG) ? 0.5f : 0.1f;   /* 短按/连按 0.1V,长按 0.5V */
      Control_SetVset(v);
      break;

    /* ---- 电压 - ---- */
    case KEY_ID_DOWN:
      v = Control_GetVset();
      v -= (evt == KEY_EVT_LONG) ? 0.5f : 0.1f;
      Control_SetVset(v);
      break;

    /* ---- 开关机(长按:清故障)---- */
    case KEY_ID_PWR:
      if (evt == KEY_EVT_LONG)
      {
        Protect_ClearFaults();
        UI_Flash("CLR");
      }
      else if (evt == KEY_EVT_SHORT)
      {
        if (Control_IsEnabled() != 0U)
        {
          Control_Disable();
          UI_Flash("OFF");
        }
        else
        {
          Control_Enable();          /* 内部会清故障 + 软启动 */
          UI_Flash("ON");
        }
      }
      break;

    /* ---- 保存(长按:恢复出厂默认)---- */
    case KEY_ID_OK:
      if (evt == KEY_EVT_LONG)
      {
        Params_t p;
        Storage_Defaults(&p);
        Control_SetVset(p.vset);
        Control_SetIlimit(p.ilimit);
        Protect_SetOcp(p.ocp);
        Control_SetMode(p.mode);
        Control_SetVpid(p.vkp, p.vki, p.vkd);
        Control_SetIpid(p.ikp, p.iki, p.ikd);
        UI_Flash("FACTORY");
      }
      else if (evt == KEY_EVT_SHORT)
      {
        uint8_t r = Storage_SaveCurrent();
        if (r == 0U)       UI_Flash("SAVED");
        else if (r == 2U)  UI_Flash("STOP 1ST");   /* 运行中不许写 Flash */
        else               UI_Flash("SAVE ERR");
      }
      break;

    default:
      break;
  }
}

/*==================== 系统时钟 ====================
 * HSE 8MHz(原理图 Y1 Value=8M)x PLL9 = 72MHz
 * APB1 = 36MHz,APB2 = 72MHz,TIM1 挂 APB2 => 100kHz 对应 ARR = 719
 * ADC 时钟 = PCLK2/6 = 12MHz
 */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef       osc = {0};
  RCC_ClkInitTypeDef       clk = {0};
  RCC_PeriphCLKInitTypeDef per = {0};

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState       = RCC_HSE_ON;
  osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  osc.HSIState       = RCC_HSI_ON;
  osc.PLL.PLLState   = RCC_PLL_ON;
  osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLMUL     = RCC_PLL_MUL9;

  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    /* HSE 起振失败:退回 HSI/2 x 16 = 64MHz。
     * 此时主频是 64MHz,开关频率会变成约 89kHz(ARR 按实际 PCLK2 算),
     * 只作应急——真出现这种情况要先查 Y1 和 C24/C25。 */
    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_ON;
    osc.HSEState            = RCC_HSE_OFF;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI_DIV2;
    osc.PLL.PLLMUL          = RCC_PLL_MUL16;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
      Error_Handler();
    }
  }

  clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;      /* 36MHz */
  clk.APB2CLKDivider = RCC_HCLK_DIV1;      /* 72MHz */
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }

  per.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  per.AdcClockSelection    = RCC_ADCPCLK2_DIV6;    /* 12MHz */
  if (HAL_RCCEx_PeriphCLKConfig(&per) != HAL_OK)
  {
    Error_Handler();
  }
}

/*==================== 错误处理 ====================
 * [!] 本板【没有 LED、没有蜂鸣器】,所以这里只能是"关输出 + 死等"。
 *     调试时在 while(1) 处打断点,或看 OLED 停在哪一屏来判断卡在哪一步。
 */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
