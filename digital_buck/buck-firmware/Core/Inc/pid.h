/**
 ******************************************************************************
 * @file    pid.h
 * @brief   PID 模块：电压外环 + 电流内环双闭环，以及控制状态管理
 ******************************************************************************
 */
#ifndef __PID_H
#define __PID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*============================== 用户配置区 ==============================*/
/* 电压外环分频：电压环周期 = PID_VLOOP_DIV × PWM 周期
 * 默认 10 → 电压环 10kHz，电流内环 100kHz（与开关频率一致） */
#define PID_VLOOP_DIV   10UL

/* 软启动时间（ms）：使能输出后，占空比上限从 0 线性升至目标值 */
#define SOFT_START_MS   50UL

/* 输出电压设定范围（V） */
#define VSET_MIN        0.8f
#define VSET_MAX        10.0f

/* 电流限制/恒流设定最大值（A） */
#define ILIMIT_MAX      3.5f

/* 工作模式 */
#define MODE_CV         0   /* 恒压：电压外环输出电流指令，电流限制仍生效 */
#define MODE_CC         1   /* 恒流：电流内环直接跟踪设定电流 */
/*=======================================================================*/

/* 增量式 PID 控制器结构体 */
typedef struct {
  float kp, ki, kd;        /* 比例 / 积分 / 微分系数（ki 单位 1/s） */
  float ts;                /* 控制周期（s） */
  float out_min, out_max;  /* 输出限幅（兼作抗积分饱和） */
  float setpoint;          /* 设定值 */
  float e1, e2;            /* 前两次误差 */
  float out;               /* 当前输出 */
} PID_t;

/* 控制状态（其他模块经 getter/setter 访问，volatile 保证跨中断可见性） */
extern volatile float   g_vset;            /* 电压设定（V） */
extern volatile float   g_ilimit;          /* 电流限制 / 恒流设定（A） */
extern volatile uint8_t g_mode;            /* MODE_CV / MODE_CC */
extern volatile uint8_t g_output_enabled;  /* 输出使能 */

/* ---- 通用 PID 操作 ---- */
void  PID_Init(PID_t *p, float kp, float ki, float kd, float ts,
               float out_min, float out_max);
void  PID_SetParams(PID_t *p, float kp, float ki, float kd);
void  PID_SetLimits(PID_t *p, float out_min, float out_max);
void  PID_Reset(PID_t *p);
float PID_Update(PID_t *p, float fb);   /* 输入反馈，返回本次输出 */
float PID_GetOut(const PID_t *p);

/* ---- 双闭环控制 ---- */
void PID_ControlInit(void);   /* 初始化双环（默认参数） */
void PID_ControlTick(void);   /* 控制周期任务：TIM1 更新中断中调用（100kHz） */

void    Control_Enable(void);
void    Control_Disable(void);
uint8_t Control_IsEnabled(void);

void  Control_SetVset(float v);
float Control_GetVset(void);
void  Control_SetIlimit(float a);
float Control_GetIlimit(void);
void  Control_SetMode(uint8_t m);
uint8_t Control_GetMode(void);

void  Control_SetVpid(float kp, float ki, float kd);
void  Control_SetIpid(float kp, float ki, float kd);
void  Control_GetVpid(float *kp, float *ki, float *kd);
void  Control_GetIpid(float *kp, float *ki, float *kd);

#ifdef __cplusplus
}
#endif

#endif /* __PID_H */
