/**
 ******************************************************************************
 * @file    storage.c
 * @brief   用 Flash 最后一页（1KB）保存参数，结构 + 异或校验
 *
 * 说明：
 *  - 写入前先比较内容，无变化则跳过擦写（延长 Flash 寿命，F1 页擦除约 20ms）；
 *  - 擦写期间 CPU 取指被 Flash 忙等待阻塞，PWM 输出保持最近占空比约几十 ms，
 *    属正常现象；如需更严格可在保存前短暂 disable 输出；
 *  - 保存操作只在 S5 / save 命令时执行，正常运行无任何 Flash 操作。
 ******************************************************************************
 */
#include "main.h"

#include <string.h>

/* 参数占用的 32 位字数量（crc 字段是最后一个字，不参与校验） */
#define STORAGE_WORDS   ((uint32_t)((sizeof(Params_t) + 3U) / 4U))
#define STORAGE_MAX_WORDS 16U   /* ≥ STORAGE_WORDS 即可 */

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
  p->magic   = STORAGE_MAGIC;
  p->version = STORAGE_VERSION;
  p->vset    = STORAGE_DEFAULT_VSET;
  p->ilimit  = STORAGE_DEFAULT_ILIMIT;
  p->ocp     = STORAGE_DEFAULT_OCP;
  p->mode    = STORAGE_DEFAULT_MODE;
  p->vkp = STORAGE_DEFAULT_VKP;  p->vki = STORAGE_DEFAULT_VKI;  p->vkd = STORAGE_DEFAULT_VKD;
  p->ikp = STORAGE_DEFAULT_IKP;  p->iki = STORAGE_DEFAULT_IKI;  p->ikd = STORAGE_DEFAULT_IKD;
  p->crc = 0;
}

/* 返回 0 = 有效；1 = 无效（填入默认值） */
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

/* 返回 0 = 写入成功（含"内容未变，跳过"的情况） */
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

/* 从 Flash 读取参数并应用到控制模块（启动时调用） */
void Storage_LoadAndApply(void)
{
  Params_t p;
  uint8_t from_flash = (Storage_Load(&p) == 0);

  Control_SetVset(p.vset);
  Control_SetIlimit(p.ilimit);
  Protect_SetOcp(p.ocp);
  Control_SetMode(p.mode);
  Control_SetVpid(p.vkp, p.vki, p.vkd);
  Control_SetIpid(p.ikp, p.iki, p.ikd);

  uart_Printf("Params: %s vset=%.3f ilim=%.3f ocp=%.3f mode=%s\r\n",
              from_flash ? "flash" : "default",
              Control_GetVset(), Control_GetIlimit(), Protect_GetOcp(),
              Control_GetMode() == MODE_CC ? "CC" : "CV");
}

/* 收集当前参数并保存（S5 短按 / save 命令），返回 0 成功 */
uint8_t Storage_SaveCurrent(void)
{
  Params_t p;

  memset(&p, 0, sizeof(p));   /* 清零 padding，避免栈垃圾导致内容比较失败而重复擦写 */
  p.magic   = STORAGE_MAGIC;
  p.version = STORAGE_VERSION;
  p.vset    = Control_GetVset();
  p.ilimit  = Control_GetIlimit();
  p.ocp     = Protect_GetOcp();
  p.mode    = Control_GetMode();
  Control_GetVpid(&p.vkp, &p.vki, &p.vkd);
  Control_GetIpid(&p.ikp, &p.iki, &p.ikd);
  p.crc = 0;
  return Storage_Save(&p);
}
