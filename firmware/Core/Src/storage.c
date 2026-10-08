/**
 ******************************************************************************
 * @file    storage.c
 * @brief   用 Flash 最后一页(1KB)保存参数,结构 + 异或校验
 *
 * 说明(沿用原版,只加了温度存档与安全门):
 *  - 写入前先比较内容,无变化则跳过擦写(延长 Flash 寿命,F1 页擦除约 20ms);
 *  - 擦写期间 CPU 取指被 Flash 忙等待阻塞,PWM 输出保持最近占空比约几十 ms --
 *    [!] 所以本工程多了一道门:只在【输出已关断】时才允许落盘。
 *    运行中擦写会让 100kHz 的过流保护断流几十 ms,那是不能接受的。
 *  - 保存操作只在 S5 键 / save 命令时执行,正常运行时无任何 Flash 操作。
 ******************************************************************************
 */
#include "main.h"
#include "storage.h"

#include <string.h>

/* 参数占用的 32 位字数量(crc 字段是最后一个字,不参与校验) */
#define STORAGE_WORDS     ((uint32_t)((sizeof(Params_t) + 3U) / 4U))
#define STORAGE_MAX_WORDS 16U   /* >= STORAGE_WORDS 即可 */

static float s_temp_max = 0.0f;

static uint32_t s_calc_crc(const Params_t *p)
{
  const uint32_t *w = (const uint32_t *)p;
  uint32_t c = 0;
  uint32_t i;

  for (i = 0; i < STORAGE_WORDS - 1U; i++) c ^= w[i];
  return c;
}

void Storage_Defaults(Params_t *p)
{
  memset(p, 0, sizeof(*p));
  p->magic    = STORAGE_MAGIC;
  p->version  = STORAGE_VERSION;
  p->vset     = STORAGE_DEFAULT_VSET;
  p->ilimit   = STORAGE_DEFAULT_ILIMIT;
  p->ocp      = STORAGE_DEFAULT_OCP;
  p->mode     = STORAGE_DEFAULT_MODE;
  p->vkp = STORAGE_DEFAULT_VKP;  p->vki = STORAGE_DEFAULT_VKI;  p->vkd = STORAGE_DEFAULT_VKD;
  p->ikp = STORAGE_DEFAULT_IKP;  p->iki = STORAGE_DEFAULT_IKI;  p->ikd = STORAGE_DEFAULT_IKD;
  p->temp_max = 0.0f;
  p->crc = 0;
}

/* 返回 0 = 有效;1 = 无效(填入默认值) */
uint8_t Storage_Load(Params_t *p)
{
  Params_t tmp;
  const uint32_t *w = (const uint32_t *)STORAGE_FLASH_ADDR;

  memcpy(&tmp, w, sizeof(Params_t));
  if (tmp.magic != STORAGE_MAGIC || tmp.version != STORAGE_VERSION ||
      tmp.crc != s_calc_crc(&tmp)) {
    Storage_Defaults(p);
    return 1;
  }
  memcpy(p, &tmp, sizeof(Params_t));
  return 0;
}

/* 返回 0 = 写入成功(含"内容未变,跳过"的情况) */
uint8_t Storage_Save(const Params_t *p)
{
  uint32_t words[STORAGE_MAX_WORDS];
  const uint32_t *cur = (const uint32_t *)STORAGE_FLASH_ADDR;
  uint32_t i, n = STORAGE_WORDS;
  uint8_t same = 1;
  Params_t tmp = *p;

  tmp.crc = s_calc_crc(&tmp);
  memcpy(words, &tmp, sizeof(Params_t));

  /* 内容相同则跳过擦写 */
  for (i = 0; i < n; i++) {
    if (cur[i] != words[i]) { same = 0; break; }
  }
  if (same) return 0;

  HAL_FLASH_Unlock();
  {
    FLASH_EraseInitTypeDef e;
    uint32_t page_err = 0;

    e.TypeErase   = FLASH_TYPEERASE_PAGES;
    e.PageAddress = STORAGE_FLASH_ADDR;
    e.NbPages     = 1;
    if (HAL_FLASHEx_Erase(&e, &page_err) != HAL_OK) {
      HAL_FLASH_Lock();
      return 1;
    }
  }
  for (i = 0; i < n; i++) {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          STORAGE_FLASH_ADDR + 4UL * i, words[i]) != HAL_OK) {
      HAL_FLASH_Lock();
      return 1;
    }
  }
  HAL_FLASH_Lock();

  /* 回读校验 */
  for (i = 0; i < n; i++) {
    if (((const uint32_t *)STORAGE_FLASH_ADDR)[i] != words[i]) return 1;
  }
  return 0;
}

/* 从 Flash 读取参数并应用到控制模块(启动时调用) */
void Storage_LoadAndApply(void)
{
  Params_t p;

  (void)Storage_Load(&p);          /* 无效时 p 已被填入默认值 */

  Control_SetVset(p.vset);
  Control_SetIlimit(p.ilimit);
  Protect_SetOcp(p.ocp);
  Control_SetMode(p.mode);
  Control_SetVpid(p.vkp, p.vki, p.vkd);
  Control_SetIpid(p.ikp, p.iki, p.ikd);

  s_temp_max = p.temp_max;
  if (s_temp_max < 0.0f) s_temp_max = 0.0f;
}

/* 收集当前参数并保存(S5 短按),返回 0 成功
 * [!] 门:输出必须已关断。运行中擦写会阻塞取指几十 ms,
 *     那段时间 100kHz 的过流保护是停摆的。 */
uint8_t Storage_SaveCurrent(void)
{
  Params_t p;

  if (Control_IsEnabled() != 0U) return 2U;   /* 2 = 因输出未关而拒绝 */

  memset(&p, 0, sizeof(p));   /* 清 padding,避免栈垃圾导致内容比较失败而重复擦写 */
  p.magic   = STORAGE_MAGIC;
  p.version = STORAGE_VERSION;
  p.vset    = Control_GetVset();
  p.ilimit  = Control_GetIlimit();
  p.ocp     = Protect_GetOcp();
  p.mode    = Control_GetMode();
  Control_GetVpid(&p.vkp, &p.vki, &p.vkd);
  Control_GetIpid(&p.ikp, &p.iki, &p.ikd);
  p.temp_max = s_temp_max;
  p.crc = 0;
  return Storage_Save(&p);
}

/* 历史最高温(主循环 1ms 调用) */
void Storage_NoteTemp(float t)
{
  if (t > s_temp_max && t < 200.0f) s_temp_max = t;
}

float Storage_GetTempMax(void)
{
  return s_temp_max;
}
