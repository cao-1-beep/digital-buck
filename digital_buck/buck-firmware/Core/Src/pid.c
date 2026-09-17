/**
 ******************************************************************************
 * @file    pid.c
 * @brief   双闭环 PID 控制：电压外环 → 电流内环 → 占空比
 *
 * 控制结构（级联控制）：
 *   电压外环（10kHz）:  输入 = Vout，输出 = 电流指令 Icmd（限幅 0 ~ Ilimit）
 *   电流内环（100kHz）: 输入 = Iout，输出 = 占空比 Duty（限幅 0 ~ 0.95）
 *   CC 模式下电压环旁路，电流内环直接跟踪设定电流。
 *
 * 增量式 PID 算法（周期 Ts）：
 *   Δu(k) = Kp·[e(k) − e(k−1)]                        （比例）
 *         + Ki·Ts·e(k)                                （积分，Ki 单位 1/s）
 *         + (Kd/Ts)·[e(k) − 2e(k−1) + e(k−2)]         （微分）
 *   u(k)  = u(k−1) + Δu(k)，输出限幅（积分自然饱和，抗积分饱和）
 *
 * 软启动：使能后占空比上限从 0 线性升至 PWM_MAX_DUTY，用时 SOFT_START_MS。
 * 保护：每个控制周期调用 Protect_Check，故障立即锁断输出。
 ******************************************************************************
 */
#include "main.h"

/* PID 初值（默认参数）定义在 storage.h（STORAGE_DEFAULT_VKP 等），
 * 与 Flash 存储的出厂默认值保持一致，实际需按系统整定（见 readme）。 */

static PID_t s_vpid;    /* 电压外环 */
static PID_t s_ipid;    /* 电流内环 */

static uint32_t s_vloop_cnt  = 0;
static uint32_t s_ss_ticks   = 0;
/* 中断（读）与主循环（写）共享，必须 volatile 防止优化器缓存 */
static volatile uint8_t s_soft_start = 0;

volatile float   g_vset           = STORAGE_DEFAULT_VSET;
volatile float   g_ilimit         = STORAGE_DEFAULT_ILIMIT;
volatile uint8_t g_mode           = STORAGE_DEFAULT_MODE;
volatile uint8_t g_output_enabled = 0;

/* ============================ 通用 PID ============================ */

void PID_Init(PID_t *p, float kp, float ki, float kd, float ts,
              float out_min, float out_max)
{
  p->kp = kp;  p->ki = ki;  p->kd = kd;
  p->ts = ts;
  p->out_min = out_min;  p->out_max = out_max;
  p->setpoint = 0.0f;
  p->e1 = 0.0f;  p->e2 = 0.0f;  p->out = 0.0f;
}

void PID_SetParams(PID_t *p, float kp, float ki, float kd)
{
  p->kp = kp;  p->ki = ki;  p->kd = kd;
}

void PID_SetLimits(PID_t *p, float out_min, float out_max)
{
  p->out_min = out_min;
  p->out_max = out_max;
  if (p->out < out_min) p->out = out_min;
  if (p->out > out_max) p->out = out_max;
}

void PID_Reset(PID_t *p)
{
  p->e1 = 0.0f;
  p->e2 = 0.0f;
  p->out = 0.0f;
}

float PID_GetOut(const PID_t *p)
{
  return p->out;
}

/* 增量式 PID 一步更新，返回限幅后的输出 */
float PID_Update(PID_t *p, float fb)
{
  float err  = p->setpoint - fb;
  float dout;

  dout = p->kp * (err - p->e1)
       + p->ki * p->ts * err
       + p->kd * (err - 2.0f * p->e1 + p->e2) / p->ts;

  p->e2 = p->e1;
  p->e1 = err;
  p->out += dout;

  if (p->out > p->out_max) p->out = p->out_max;
  else if (p->out < p->out_min) p->out = p->out_min;

  return p->out;
}

/* ============================ 双闭环控制 ============================ */

void PID_ControlInit(void)
{
  /* 电压环 Ts = 10 × 10us = 100us；电流环 Ts = 10us
   * 默认参数（起始值，可在线整定后 save）定义在 storage.h */
  PID_Init(&s_vpid, STORAGE_DEFAULT_VKP, STORAGE_DEFAULT_VKI, STORAGE_DEFAULT_VKD,
           (float)PID_VLOOP_DIV / (float)PWM_FREQ_HZ,
           0.0f, STORAGE_DEFAULT_ILIMIT);
  PID_Init(&s_ipid, STORAGE_DEFAULT_IKP, STORAGE_DEFAULT_IKI, STORAGE_DEFAULT_IKD,
           1.0f / (float)PWM_FREQ_HZ,
           0.0f, PWM_MAX_DUTY);
  s_vpid.setpoint = g_vset;
  s_vloop_cnt = 0;
  s_ss_ticks  = 0;
  s_soft_start = 0;
  g_output_enabled = 0;
}

/* 控制周期任务：在 TIM1 更新中断（100kHz）中调用 */
void PID_ControlTick(void)
{
  float vout, iout, vin;
  float icmd, duty, duty_limit, ramp;

  if (!g_output_enabled) {
    PWM_SetDuty(0.0f);
    return;
  }

  /* 读取最新采样（DMA 环形缓冲，4 轮平均） */
  ADC_GetMeasurements(&vout, &iout, &vin);

  /* 保护检测：过流 / 过压 / 欠压，触发后立即锁断 */
  Protect_Check(vout, iout, vin);
  if (!g_output_enabled) {
    PWM_Disable();
    PWM_SetDuty(0.0f);
    return;
  }

  /* 软启动：占空比上限从 0 线性升至 PWM_MAX_DUTY（SOFT_START_MS 内完成） */
  duty_limit = PWM_MAX_DUTY;
  if (s_soft_start) {
    s_ss_ticks++;
    ramp = (float)s_ss_ticks /
           (float)(SOFT_START_MS * PWM_FREQ_HZ / 1000UL);
    if (ramp >= 1.0f) {
      ramp = 1.0f;
      s_soft_start = 0;
    }
    duty_limit = ramp * PWM_MAX_DUTY;
  }

  /* 电流指令：恒流模式直接取设定值；恒压模式取电压环输出并限幅 */
  if (g_mode == MODE_CC) {
    icmd = g_ilimit;
  } else {
    s_vpid.setpoint = g_vset;
    if ((++s_vloop_cnt % PID_VLOOP_DIV) == 0)
      PID_Update(&s_vpid, vout);
    icmd = PID_GetOut(&s_vpid);
    if (icmd < 0.0f)        icmd = 0.0f;
    if (icmd > g_ilimit)    icmd = g_ilimit;
  }

  /* 电流内环：输出占空比 */
  s_ipid.setpoint = icmd;
  duty = PID_Update(&s_ipid, iout);
  if (duty < 0.0f)      duty = 0.0f;
  if (duty > duty_limit) duty = duty_limit;

  PWM_SetDuty(duty);
}

/* 使能输出：清故障 → 复位双环 → 软启动 → 打开 PWM */
void Control_Enable(void)
{
  Protect_ClearFaults();
  PID_Reset(&s_vpid);
  PID_Reset(&s_ipid);
  s_vpid.setpoint = g_vset;
  s_soft_start = 1;
  s_ss_ticks  = 0;
  g_output_enabled = 1;
  PWM_Enable();
}

void Control_Disable(void)
{
  g_output_enabled = 0;
  s_soft_start = 0;
  PWM_Disable();
  PWM_SetDuty(0.0f);
  PID_Reset(&s_vpid);
  PID_Reset(&s_ipid);
}

uint8_t Control_IsEnabled(void)
{
  return g_output_enabled;
}

/* ============================ 参数读写 ============================ */

void Control_SetVset(float v)
{
  if (v < VSET_MIN) v = VSET_MIN;
  if (v > VSET_MAX) v = VSET_MAX;
  g_vset = v;
  s_vpid.setpoint = v;
}

float Control_GetVset(void)
{
  return g_vset;
}

void Control_SetIlimit(float a)
{
  if (a < 0.1f)    a = 0.1f;
  if (a > ILIMIT_MAX) a = ILIMIT_MAX;
  g_ilimit = a;
  /* 电压环输出（电流指令）同步限幅 */
  PID_SetLimits(&s_vpid, 0.0f, a);
}

float Control_GetIlimit(void)
{
  return g_ilimit;
}

void Control_SetMode(uint8_t m)
{
  g_mode = (m != 0) ? MODE_CC : MODE_CV;
  PID_Reset(&s_vpid);
  PID_Reset(&s_ipid);
}

uint8_t Control_GetMode(void)
{
  return g_mode;
}

void Control_SetVpid(float kp, float ki, float kd)
{
  PID_SetParams(&s_vpid, kp, ki, kd);
}

void Control_SetIpid(float kp, float ki, float kd)
{
  PID_SetParams(&s_ipid, kp, ki, kd);
}

void Control_GetVpid(float *kp, float *ki, float *kd)
{
  *kp = s_vpid.kp;  *ki = s_vpid.ki;  *kd = s_vpid.kd;
}

void Control_GetIpid(float *kp, float *ki, float *kd)
{
  *kp = s_ipid.kp;  *ki = s_ipid.ki;  *kd = s_ipid.kd;
}
