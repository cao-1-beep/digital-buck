/**
 ******************************************************************************
 * @file    pid.c
 * @brief   双闭环实现 + 100kHz 控制中断入口
 *
 * 中断里的工作量(这是本文件最该被审的地方):
 *   ADC_OnPwmTick()          : 1 次整数转浮点 + 1 次减法 + 1 次除法
 *   Control_OnSample() 每周期: 2 次整数比较 + 1 次取模
 *                      每 2 周期: 1 次 PID(约 8 次浮点运算)
 *                      每 20 周期: 再 1 次 PID
 *   => 最坏周期约 12~15 次浮点运算, 估算 300~400 个周期
 *      预算 720 个周期(10us @72MHz) => 余量约 2 倍。
 *   [!] 上板后请用一个空闲 GPIO 在中断进出处翻转,示波器量实际宽度,
 *       不要相信估算,也不要相信"约数 us 属于正常设计"这种说法。
 ******************************************************************************
 */
#include "main.h"
#include "pid.h"
#include "adc.h"
#include "pwm.h"
#include "protect.h"

/*==================== 控制状态 ====================*/
volatile float   g_vset         = 5.0f;
volatile float   g_ilimit       = OCP_LIMIT_DEFAULT;
volatile uint8_t g_mode         = MODE_CV;
volatile uint8_t g_output_enabled = 0U;
volatile float   g_vset_eff     = 5.0f;
volatile float   g_ilimit_eff   = OCP_LIMIT_DEFAULT;

static PID_t    s_vpid;
static PID_t    s_ipid;
static uint32_t s_tick         = 0U;
static uint16_t s_trip_cnt     = 0U;
static uint8_t  s_limit_hit    = 0U;
static uint32_t s_limit_events = 0U;
static float    s_icmd         = 0.0f;
static uint32_t s_ss_ticks     = 0U;
static uint32_t s_ss_total     = 1U;
static uint8_t  s_soft_start   = 0U;

/*==================== 通用 PID(增量式,沿用原版算法)====================*/
void PID_Init(PID_t *p, float kp, float ki, float kd, float ts,
              float out_min, float out_max)
{
  p->kp = kp; p->ki = ki; p->kd = kd; p->ts = ts;
  p->out_min = out_min; p->out_max = out_max;
  p->setpoint = 0.0f; p->e1 = 0.0f; p->e2 = 0.0f; p->out = 0.0f;
}

void PID_SetParams(PID_t *p, float kp, float ki, float kd)
{
  p->kp = kp; p->ki = ki; p->kd = kd;
}

void PID_Reset(PID_t *p)
{
  p->e1 = 0.0f; p->e2 = 0.0f; p->out = 0.0f;
}

float PID_GetOut(const PID_t *p)
{
  return p->out;
}

float PID_Update(PID_t *p, float fb)
{
  float e  = p->setpoint - fb;
  float du = p->kp * (e - p->e1)
           + p->ki * p->ts * e
           + (p->kd / p->ts) * (e - 2.0f * p->e1 + p->e2);
  float u  = p->out + du;

  if (u > p->out_max) u = p->out_max;      /* 输出限幅 = 抗积分饱和 */
  if (u < p->out_min) u = p->out_min;

  p->e2 = p->e1;
  p->e1 = e;
  p->out = u;
  return u;
}

/*==================== 双闭环初始化 ====================*/
void PID_ControlInit(void)
{
  /* 电流内环:反馈输出电流(A) -> 输出占空比;Ts = 20us */
  PID_Init(&s_ipid, IPID_KP_DEFAULT, IPID_KI_DEFAULT, IPID_KD_DEFAULT,
           1.0f / (float)(PWM_FREQ_HZ / PID_ILOOP_DIV),
           0.0f, PWM_MAX_DUTY);

  /* 电压外环:反馈输出电压(V) -> 输出电流指令(A);Ts = 200us */
  PID_Init(&s_vpid, VPID_KP_DEFAULT, VPID_KI_DEFAULT, VPID_KD_DEFAULT,
           (float)PID_VLOOP_DIV / (float)PWM_FREQ_HZ,
           0.0f, ILIMIT_MAX);

  s_vpid.setpoint = g_vset;
  g_vset_eff      = g_vset;
  g_ilimit_eff    = g_ilimit;
  s_icmd          = 0.0f;
  s_limit_events  = 0U;
  s_tick          = 0U;

  /* 软启动总拍数(按电流环节拍数算) */
  s_ss_total = (SOFT_START_MS * (PWM_FREQ_HZ / 1000UL)) / PID_ILOOP_DIV;
  if (s_ss_total == 0U) s_ss_total = 1U;
}

/*==================== 使能 / 禁止 ====================*/
void Control_Enable(void)
{
  Protect_ClearFaults();          /* 使能时自动清故障(和原版行为一致) */
  PID_Reset(&s_ipid);
  PID_Reset(&s_vpid);
  s_vpid.setpoint = g_vset_eff;
  s_ipid.setpoint = 0.0f;
  s_icmd          = 0.0f;
  s_soft_start    = 1U;
  s_ss_ticks      = 0U;
  s_limit_events  = 0U;
  s_trip_cnt      = 0U;
  g_output_enabled = 1U;
  PWM_Enable();
}

void Control_Disable(void)
{
  g_output_enabled = 0U;
  s_soft_start     = 0U;
  PID_Reset(&s_ipid);
  PID_Reset(&s_vpid);
  s_icmd = 0.0f;
  PWM_Disable();                  /* 只清 MOE;计数器和中断继续跑 */
}

uint8_t Control_IsEnabled(void)
{
  return g_output_enabled;
}

/*==================== 主循环 1ms ====================*/
void Control_Tick1ms(void)
{
  float vin  = ADC_GetVin();
  float vout = ADC_GetVout();
  float t    = ADC_GetTempC();
  float sc, lim;

  /* ① 过温降额 -> 有效限流点(浮点只在这里算) */
  sc = Protect_ThermalScale();
  if (sc < 0.0f) sc = 0.0f;
  if (sc > 1.0f) sc = 1.0f;
  g_ilimit_eff = g_ilimit * sc;
  if (g_ilimit_eff < ILIMIT_MIN) g_ilimit_eff = ILIMIT_MIN;

  /* ② 电压设定钳位:Vout <= 0.85 x Vin(自举充电时间) */
  lim = VOUT_HEADROOM * vin;
  if (lim < VSET_MIN) lim = VSET_MIN;
  g_vset_eff = (g_vset < lim) ? g_vset : lim;

  /* ③ 慢保护:输入欠压/过压、输出过压、过温 */
  Protect_CheckSlow(vin, vout, t, g_vset_eff);
}

/*==================== 100kHz 中断里调用 ====================*/
void Control_OnSample(uint16_t raw, float amps)
{
  float duty, dmax, ramp;

  s_tick++;

  /* ---- ① 逐周期限流:纯整数比较,最快的一层 ---- */
  s_limit_hit = (raw >= g_limit_raw) ? 1U : 0U;

  /* ---- ② 硬跳闸:连续 OCP_DEBOUNCE 个 PWM 周期超跳闸点 ---- */
  if (raw >= g_trip_raw)
  {
    if (s_trip_cnt < 0xFFFFU) s_trip_cnt++;
  }
  else
  {
    s_trip_cnt = 0U;
  }
  if (s_trip_cnt >= OCP_DEBOUNCE)
  {
    Protect_Trip(FAULT_OCP);       /* 内部会 Control_Disable */
  }

  if (g_output_enabled == 0U)
  {
    PWM_SetDuty(0.0f);
    return;
  }

  /* 本周期过流:占空比压 0,下周期自动恢复(这就是逐周期限流) */
  if (s_limit_hit != 0U)
  {
    s_limit_events++;
    PWM_SetDuty(0.0f);
    return;
  }

  /* ---- ③ 电流内环节拍:每 PID_ILOOP_DIV 个周期算一次 ---- */
  if ((s_tick % PID_ILOOP_DIV) != 0U) return;

  /* 软启动:限制占空比上限 */
  dmax = PWM_MAX_DUTY;
  if (s_soft_start != 0U)
  {
    if (s_ss_ticks < s_ss_total) s_ss_ticks++;
    ramp = (float)s_ss_ticks / (float)s_ss_total;
    if (ramp >= 1.0f)
    {
      ramp = 1.0f;
      s_soft_start = 0U;
    }
    dmax = ramp * PWM_MAX_DUTY;
  }

  /* ---- ④ 电压外环节拍:每 PID_VLOOP_DIV 个周期算一次 ---- */
  if ((s_tick % PID_VLOOP_DIV) == 0U)
  {
    s_vpid.setpoint = g_vset_eff;
    s_vpid.out_max  = g_ilimit_eff;
    (void)PID_Update(&s_vpid, ADC_GetVout());
  }

  /* ---- ⑤ 电流指令 + 电流内环 ---- */
  if (g_mode == MODE_CC)
  {
    s_icmd = g_ilimit_eff;
  }
  else
  {
    s_icmd = PID_GetOut(&s_vpid);
    if (s_icmd < 0.0f)         s_icmd = 0.0f;
    if (s_icmd > g_ilimit_eff) s_icmd = g_ilimit_eff;
  }

  s_ipid.setpoint = s_icmd;
  duty = PID_Update(&s_ipid, amps);
  if (duty > dmax) duty = dmax;
  if (duty < 0.0f) duty = 0.0f;
  PWM_SetDuty(duty);
}

/*==================== 100kHz 中断入口 ====================
 * TIM1 更新中断 -> stm32f1xx_it.c 的 TIM1_UP_IRQHandler
 *                -> HAL_TIM_IRQHandler -> 这里
 * [!] 全工程只能有这一处 HAL_TIM_PeriodElapsedCallback 定义。
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance != TIM1) return;

  ADC_OnPwmTick();                                   /* 更新电流 + 扫描计数 */
  Control_OnSample(ADC_GetIoutRaw(), ADC_GetIout()); /* 限流 + 分频 PID */
}

/*==================== 参数读写 ====================*/
void Control_SetVset(float v)
{
  if (v < VSET_MIN) v = VSET_MIN;
  if (v > VSET_MAX) v = VSET_MAX;
  g_vset = v;
}

float Control_GetVset(void) { return g_vset; }

void Control_SetIlimit(float a)
{
  if (a < ILIMIT_MIN) a = ILIMIT_MIN;
  if (a > ILIMIT_MAX) a = ILIMIT_MAX;
  g_ilimit = a;
  Protect_SetLimit(a);        /* 同步刷新逐周期限流的整数阈值 */
}

float Control_GetIlimit(void) { return g_ilimit; }

void Control_SetMode(uint8_t m) { g_mode = (m == MODE_CC) ? MODE_CC : MODE_CV; }
uint8_t Control_GetMode(void)   { return g_mode; }

void Control_SetVpid(float kp, float ki, float kd) { PID_SetParams(&s_vpid, kp, ki, kd); }
void Control_SetIpid(float kp, float ki, float kd) { PID_SetParams(&s_ipid, kp, ki, kd); }

void Control_GetVpid(float *kp, float *ki, float *kd)
{
  *kp = s_vpid.kp; *ki = s_vpid.ki; *kd = s_vpid.kd;
}

void Control_GetIpid(float *kp, float *ki, float *kd)
{
  *kp = s_ipid.kp; *ki = s_ipid.ki; *kd = s_ipid.kd;
}

float    Control_GetIcmd(void)        { return s_icmd; }
uint8_t  Control_GetLimitHit(void)    { return s_limit_hit; }
uint32_t Control_GetLimitEvents(void) { return s_limit_events; }
