/**
 ******************************************************************************
 * @file    protect.c
 * @brief   保护实现:逐周期限流阈值换算 + 硬跳闸 + 慢保护 + 过温降额
 *
 * 关键设计:所有"电流 -> 码值"的换算只在初始化和改设定时做一次,
 * 100kHz 中断里只做【整数比较】。
 ******************************************************************************
 */
#include "main.h"
#include "protect.h"
#include "adc.h"
#include "pid.h"

volatile uint8_t  g_fault     = 0U;
volatile uint16_t g_limit_raw = 0xFFFFU;
volatile uint16_t g_trip_raw  = 0xFFFFU;

static volatile float s_ocp   = OCP_TRIP_A;
static volatile float s_limit = OCP_LIMIT_DEFAULT;

static uint16_t s_uvp_cnt   = 0U;
static uint16_t s_ovin_cnt  = 0U;
static uint16_t s_ovout_cnt = 0U;
static uint16_t s_otp_cnt   = 0U;

/*==================== 电流 -> 原始码值 ====================*/
static uint16_t cur_to_raw(float a)
{
  float d, r;

  /* 相对标定零点的码值差:V = I x 0.4706 V/A,1 码 = 0.8057 mV */
  d = (a * ADC_IOUT_V_PER_A) / ADC_LSB_V;
  r = (float)ADC_GetZeroRaw() + d;

  if (r < 0.0f)    r = 0.0f;
  if (r > 4095.0f) r = 4095.0f;
  return (uint16_t)r;
}

/*==================== 置故障 ====================*/
static void set_fault(uint8_t f)
{
  if ((g_fault & f) == 0U)
  {
    g_fault |= f;
    Control_Disable();        /* 首次触发才关输出 */
  }
}

/*==================== 初始化 / 设定 ====================*/
void Protect_Init(void)
{
  g_fault = 0U;
  s_limit = OCP_LIMIT_DEFAULT;
  s_ocp   = OCP_TRIP_A;

  g_limit_raw = cur_to_raw(s_limit);
  g_trip_raw  = cur_to_raw(s_ocp);

  s_uvp_cnt = s_ovin_cnt = s_ovout_cnt = s_otp_cnt = 0U;
}

/* 限流点(CC 工作阈值):刷 g_limit_raw */
void Protect_SetLimit(float a)
{
  if (a < OCP_LIMIT_MIN) a = OCP_LIMIT_MIN;
  if (a > OCP_LIMIT_MAX) a = OCP_LIMIT_MAX;
  s_limit = a;
  g_limit_raw = cur_to_raw(a);      /* 整数阈值同步刷新,中断里不用再算 */
}

float Protect_GetLimit(void)
{
  return s_limit;
}

/* 过流跳闸阈值:刷 g_trip_raw。
 * [!] 上限卡在 3.6A,因为电感 L1 的饱和电流是 3.8A --
 *     跳闸点必须留在饱和点之下,否则电感先饱和,后面电流失控。 */
void Protect_SetOcp(float a)
{
  if (a < OCP_TRIP_MIN) a = OCP_TRIP_MIN;
  if (a > OCP_TRIP_MAX) a = OCP_TRIP_MAX;
  s_ocp = a;
  g_trip_raw = cur_to_raw(a);
}

float Protect_GetOcp(void)
{
  return s_ocp;
}

void Protect_ClearFaults(void)
{
  g_fault = 0U;
  s_uvp_cnt = s_ovin_cnt = s_ovout_cnt = s_otp_cnt = 0U;
}

/*==================== 100kHz 中断调用(中断安全)====================*/
/* 只写标志 + 调用 Control_Disable(它只改全局变量和 CCR/MOE 寄存器) */
void Protect_Trip(uint8_t fault)
{
  set_fault(fault);
}

/*==================== 过温降额 ====================*/
float Protect_ThermalScale(void)
{
  float t = ADC_GetTempC();

  if (t < -50.0f)        return 1.0f;              /* 温度无效 => 不降额 */
  if (t <= OTP_DERATE_C) return 1.0f;
  if (t >= OTP_TRIP_C)   return OTP_DERATE_FLOOR;  /* 85C 及以上收到 50% */

  return 1.0f - (1.0f - OTP_DERATE_FLOOR) *
                (t - OTP_DERATE_C) / (OTP_TRIP_C - OTP_DERATE_C);
}

/*==================== 主循环 1ms 调用 ====================*/
void Protect_CheckSlow(float vin, float vout, float temp_c, float vset)
{
  /* --- ① 输入欠压:低于阈值连续 50ms 置位;回到 阈值+0.5V 自动清除该位
   *        (但输出不会自动重新打开 -- 要用户按开关机键) --- */
  if (vin < UVP_THRESHOLD)
  {
    if (s_uvp_cnt < 0xFFFFU) s_uvp_cnt++;
  }
  else if (vin > (UVP_THRESHOLD + UVP_HYST))
  {
    s_uvp_cnt = 0U;
    g_fault &= (uint8_t)~FAULT_UVP;
  }
  if (s_uvp_cnt >= UVP_DEBOUNCE) set_fault(FAULT_UVP);

  /* --- ② 输入过压:高于 26.5V 连续 20ms;回到 26.0V 以下清除 --- */
  if (vin > OVP_VIN_MAX)
  {
    if (s_ovin_cnt < 0xFFFFU) s_ovin_cnt++;
  }
  else if (vin < (OVP_VIN_MAX - OVP_VIN_HYST))
  {
    s_ovin_cnt = 0U;
    g_fault &= (uint8_t)~FAULT_OVP_VIN;
  }
  if (s_ovin_cnt >= OVP_VIN_DEBOUNCE) set_fault(FAULT_OVP_VIN);

  /* --- ③ 输出过压:超过 设定值x1.10 或 绝对上限 11.0V,连续 3ms --- */
  {
    float lim = vset * OVP_VOUT_RATIO;
    if (lim > OVP_VOUT_ABS) lim = OVP_VOUT_ABS;
    if (vout > lim)
    {
      if (s_ovout_cnt < 0xFFFFU) s_ovout_cnt++;
    }
    else
    {
      s_ovout_cnt = 0U;
    }
    if (s_ovout_cnt >= OVP_VOUT_DEBOUNCE) set_fault(FAULT_OVP_VOUT);
  }

  /* --- ④ 过温:>85C 连续 100ms 锁存;回落到 75C(85-10)以下才允许清除 --- */
  if (temp_c > OTP_TRIP_C)
  {
    if (s_otp_cnt < 0xFFFFU) s_otp_cnt++;
  }
  else if (temp_c < (OTP_TRIP_C - OTP_HYST_C))
  {
    s_otp_cnt = 0U;
    g_fault &= (uint8_t)~FAULT_OTP;
  }
  if (s_otp_cnt >= OTP_DEBOUNCE) set_fault(FAULT_OTP);
}

/*==================== 故障短名(OLED 用)====================*/
const char *Protect_FaultStr(void)
{
  uint8_t f = g_fault;

  if (f == 0U)                 return "OK   ";
  if (f & FAULT_OCP)           return "OCP  ";   /* 输出过流 */
  if (f & FAULT_OTP)           return "OTP  ";   /* 过温   */
  if (f & FAULT_OVP_VOUT)      return "OVOUT";   /* 输出过压 */
  if (f & FAULT_OVP_VIN)       return "OVIN ";   /* 输入过压 */
  if (f & FAULT_UVP)           return "UVIN ";   /* 输入欠压 */
  return "FAULT";
}
