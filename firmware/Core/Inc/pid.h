/**
 ******************************************************************************
 * @file    pid.h
 * @brief   双闭环控制(电压外环 + 电流内环)+ 100kHz 控制中断的入口
 *
 * 节拍分工 —— 这是本工程控制部分最核心的设计:
 *   100kHz(每 10us):读电流 -> 逐周期限流(整数比较)-> 计数分频
 *    50kHz(每 2 次):电流内环 PID -> 占空比
 *     5kHz(每 20 次):电压外环 PID -> 电流指令
 *
 * 为什么必须这么分(不是偷懒,是算出来的):
 *   ① 电流采样链的带宽 = LMV321 的 GBW(1MHz) / 闭环增益(15.7) ≈ 64kHz。
 *      所以"100kHz 的电流环"买不到更快的响应,只是让 CPU 更累。
 *   ② 原版在 10us 的预算里做 ADC 的浮点平均(12 次整数转浮点 + 12 次浮点加)
 *      + 保护 + PID,实测估算 15~29us,超预算 1.5~3 倍(会漏 PWM 更新)。
 *   ③ 分成 50kHz 之后,电流环每 20us 算一次,工作量只有几百个周期,余量充足。
 *
 * [!] 与代码原版在电流采样上的差别:
 *   原版按"增益 50 / 10mR / 无偏置"换算,而实际板上(你改过之后)是
 *   R11 = 30mR、增益 15.7、偏置 0.3765V => 见 adc.h
 ******************************************************************************
 */
#ifndef __PID_H
#define __PID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*============================== 用户配置区 ==============================*/
/* 分频:电压环 = PID_VLOOP_DIV x 10us;电流环 = PID_ILOOP_DIV x 10us */
#define PID_ILOOP_DIV    2UL      /* 2 x 10us = 20us  => 50kHz 电流环 */
#define PID_VLOOP_DIV    20UL     /* 20 x 10us = 200us => 5kHz 电压环 */

/* 软启动:使能输出后,占空比上限从 0 线性升到 PWM_MAX_DUTY */
#define SOFT_START_MS    50UL

/* 输出电压设定范围(本工程规格:5~10V) */
#define VSET_MIN         5.0f
#define VSET_MAX         10.0f

/* 限流/恒流设定范围(电感额定 3A、饱和 3.8A;你定 2A) */
#define ILIMIT_MIN       0.1f
#define ILIMIT_MAX       2.0f

/* 输出电压相对输入的上限:Vout <= 0.85 x Vin
 * 不是为了防直通,而是 RT9624 的自举电容要靠关断期充电 */
#define VOUT_HEADROOM    0.85f

/* 工作模式 */
#define MODE_CV          0        /* 恒压:电压环给电流指令,内环限流 */
#define MODE_CC          1        /* 恒流:内环直接跟设定电流 */

/* PID 初始参数(台架起点,可用 SWO/OLED 在线改)
 * 电流环:输入 A,输出占空比;Ts = 20us
 * 电压环:输入 V,输出 A;      Ts = 200us */
#define IPID_KP_DEFAULT  0.20f
#define IPID_KI_DEFAULT  800.0f
#define IPID_KD_DEFAULT  0.0f
#define VPID_KP_DEFAULT  0.40f
#define VPID_KI_DEFAULT  150.0f
#define VPID_KD_DEFAULT  0.0f
/*=======================================================================*/

/* 增量式 PID 控制器 */
typedef struct
{
  float kp, ki, kd;        /* 比例 / 积分 / 微分系数(ki 单位 1/s) */
  float ts;                /* 控制周期 (s) */
  float out_min, out_max;  /* 输出限幅(兼作抗积分饱和) */
  float setpoint;          /* 设定值 */
  float e1, e2;            /* 前两次误差 */
  float out;               /* 当前输出 */
} PID_t;

/* 控制状态(其他模块只通过 getter/setter 访问;volatile 保证跨中断可见) */
extern volatile float   g_vset;            /* 电压设定 (V) */
extern volatile float   g_ilimit;          /* 限流设定 (A) */
extern volatile uint8_t g_mode;            /* MODE_CV / MODE_CC */
extern volatile uint8_t g_output_enabled;  /* 输出使能 */

/* 由主循环 1ms 更新、供 100kHz 中断直接读取的三个"预算好的值"
 * (中断里不许做浮点换算,只许读) */
extern volatile float   g_vset_eff;        /* 钳位后的电压设定 */
extern volatile float   g_ilimit_eff;      /* 过温降额后的限流点 */

/* ---- 通用 PID ---- */
void  PID_Init(PID_t *p, float kp, float ki, float kd, float ts,
               float out_min, float out_max);
void  PID_SetParams(PID_t *p, float kp, float ki, float kd);
void  PID_Reset(PID_t *p);
float PID_Update(PID_t *p, float fb);
float PID_GetOut(const PID_t *p);

/* ---- 双闭环 ---- */
void    PID_ControlInit(void);
void    Control_Enable(void);
void    Control_Disable(void);
uint8_t Control_IsEnabled(void);

/* 主循环 1ms:算降额/钳位、跑慢保护 */
void    Control_Tick1ms(void);

/* [!] 100kHz 中断里调用(由 HAL_TIM_PeriodElapsedCallback 转进来)。
 *     raw = 电流通道原始码值(整数限流用), amps = 换算好的电流 */
void    Control_OnSample(uint16_t raw, float amps);

/* ---- 参数读写 ---- */
void    Control_SetVset(float v);
float   Control_GetVset(void);
void    Control_SetIlimit(float a);
float   Control_GetIlimit(void);
void    Control_SetMode(uint8_t m);
uint8_t Control_GetMode(void);
void    Control_SetVpid(float kp, float ki, float kd);
void    Control_SetIpid(float kp, float ki, float kd);
void    Control_GetVpid(float *kp, float *ki, float *kd);
void    Control_GetIpid(float *kp, float *ki, float *kd);
float   Control_GetIcmd(void);
uint8_t Control_GetLimitHit(void);
uint32_t Control_GetLimitEvents(void);

#ifdef __cplusplus
}
#endif

#endif /* __PID_H */
