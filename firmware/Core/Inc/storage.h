/**
 ******************************************************************************
 * @file    storage.h
 * @brief   参数存储:利用片内 Flash 最后一页模拟 EEPROM
 *
 * [!] 与代码原版的差别:
 *   ① 默认值按本工程规格改(Vset 5.0V、限流 2.0A、跳闸 3.2A)
 *   ② 结构体加了一个 temp_max(历史最高温,给故障复现用)
 *      => 尺寸 52 -> 56 字节,所以 STORAGE_VERSION 升到 2
 ******************************************************************************
 */
#ifndef __STORAGE_H
#define __STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*============================== 用户配置区 ==============================*/
/* 存储页地址:F103C8T6 共 64KB(0x08000000~0x0800FFFF,每页 1KB),
 * 使用最后一页 0x0800FC00。若换成 128KB 的芯片(如 RBT6)请改为 0x0801FC00。
 * 注意:固件体积需小于 63KB,且不要把代码放到最后一页。 */
#define STORAGE_FLASH_ADDR  0x0800FC00UL

#define STORAGE_MAGIC       0x44424331UL   /* "DBC1" */
#define STORAGE_VERSION     2U             /* 2:加了 temp_max */

/* 出厂默认参数(未保存过 / Flash 无效时使用,也是恢复出厂值) */
#define STORAGE_DEFAULT_VSET    5.0f
#define STORAGE_DEFAULT_ILIMIT  2.0f         /* 限流(CC 工作阈值) */
#define STORAGE_DEFAULT_OCP     3.2f         /* 过流跳闸 */
#define STORAGE_DEFAULT_MODE    0            /* MODE_CV */
#define STORAGE_DEFAULT_VKP     0.40f        /* 电压环 Kp(A/V) */
#define STORAGE_DEFAULT_VKI     150.0f       /* 电压环 Ki(1/s) */
#define STORAGE_DEFAULT_VKD     0.0f
#define STORAGE_DEFAULT_IKP     0.20f        /* 电流环 Kp(占空比/A) */
#define STORAGE_DEFAULT_IKI     800.0f       /* 电流环 Ki(1/s) */
#define STORAGE_DEFAULT_IKD     0.0f
/*=======================================================================*/

/* 存储结构:56 字节 = 14 个字,crc 为最后一个字,异或校验前 13 个字 */
typedef struct {
  uint32_t magic;      /* 魔数,校验存储有效性 */
  uint16_t version;    /* 结构版本 */
  float    vset;       /* 电压设定(V) */
  float    ilimit;     /* 电流限制 / 恒流设定(A) */
  float    ocp;        /* 过流跳闸阈值(A) */
  uint8_t  mode;       /* MODE_CV / MODE_CC */
  float    vkp, vki, vkd;   /* 电压环 PID */
  float    ikp, iki, ikd;   /* 电流环 PID */
  float    temp_max;        /* 历史最高温(C),故障复现用 */
  uint32_t crc;        /* 校验和 */
} Params_t;

/* 编译期断言:锁死 Params_t 为 56 字节,保证 Flash 布局稳定 */
typedef char PARAMS_SIZE_CHECK[(sizeof(Params_t) == 56U) ? 1 : -1];

/* 返回 0 = Flash 有效并载入;返回 1 = Flash 无效,填入默认值 */
uint8_t Storage_Load(Params_t *p);
/* 返回 0 = 写入成功 */
uint8_t Storage_Save(const Params_t *p);
void    Storage_Defaults(Params_t *p);

/* 便捷封装:读取并应用 / 收集当前参数并保存(返回 0 成功) */
void    Storage_LoadAndApply(void);
uint8_t Storage_SaveCurrent(void);

/* 历史最高温(掉电保存) */
void    Storage_NoteTemp(float t);       /* 主循环 1ms 调用:刷新最大值 */
float   Storage_GetTempMax(void);

#ifdef __cplusplus
}
#endif

#endif /* __STORAGE_H */
