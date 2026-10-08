/**
 ******************************************************************************
 * @file    uart.c
 * @brief   USART1（PA9 TX / PA10 RX）115200-8N1
 *
 * - 接收：RXNE 中断 + 256 字节环形缓冲，主循环 UART_Poll() 按行解析命令；
 * - 发送：轮询 TXE（阻塞式，命令响应/周期上报场景足够）；
 * - 输出：自定义轻量 printf（%s %c %d %u %x %f 支持 %.Nf 精度），
 *   不依赖 newlib/MicroLIB 的浮点 printf，避免链接选项问题；
 *
 * 命令协议（详见 readme）：
 *   set v 5.0 / set i 2.5 / set ocp 3.5
 *   get v | get i | get vin | get duty | get mode | get status
 *   pid [v|i] p <kp> i <ki> d <kd> / pid
 *   enable / disable / mode cv|cc / save / clear / status / help
 * 返回：OK <数据> 或 ERROR <原因>
 ******************************************************************************
 */
#include "main.h"

#include <stdarg.h>
#include <string.h>

UART_HandleTypeDef huart1;

/* ------------------------- 接收环形缓冲 ------------------------- */
#define UART_RX_BUF_SIZE    256
static volatile uint8_t  s_rx_ring[UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0;
static volatile uint16_t s_rx_tail = 0;

#define UART_LINE_MAX       64
static char     s_line[UART_LINE_MAX];
static uint16_t s_line_len = 0;

/* ------------------------- 发送（阻塞轮询 TXE） ------------------------- */
static void uart_SendByte(uint8_t b)
{
  while (!__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE)) { }
  huart1.Instance->DR = b;
}

void uart_Puts(const char *s)
{
  while (*s) uart_SendByte((uint8_t)*s++);
}

static void uart_PutUInt(uint32_t v)
{
  char t[12];
  uint8_t n = 0;
  do {
    t[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  while (n) uart_SendByte((uint8_t)t[--n]);
}

static void uart_PutInt(int32_t v)
{
  if (v < 0) {
    uart_SendByte('-');
    v = -v;
  }
  uart_PutUInt((uint32_t)v);
}

static void uart_PutHex(uint32_t v)
{
  char t[10];
  uint8_t n = 0;
  do {
    uint32_t d = v & 0xF;
    t[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
    v >>= 4;
  } while (v);
  while (n) uart_SendByte((uint8_t)t[--n]);
}

/* 定点方式打印浮点（%f 默认 2 位小数，%.Nf 可指定 0~4 位） */
static void uart_PutFloat(float v, int prec)
{
  static const uint32_t p10[5] = { 1, 10, 100, 1000, 10000 };
  uint32_t ip, frac;
  int i;

  if (prec < 0) prec = 0;
  if (prec > 4) prec = 4;
  if (v < 0.0f) {
    uart_SendByte('-');
    v = -v;
  }
  ip = (uint32_t)v;
  frac = (uint32_t)((v - (float)ip) * (float)p10[prec] + 0.5f);
  if (frac >= p10[prec]) {   /* 四舍五入进位 */
    frac = 0;
    ip++;
  }
  uart_PutUInt(ip);
  if (prec > 0) {
    uart_SendByte('.');
    for (i = prec - 1; i >= 0; i--)
      uart_SendByte((uint8_t)('0' + (frac / p10[i]) % 10));
  }
}

void uart_Printf(const char *fmt, ...)
{
  va_list ap;
  const char *p = fmt;
  int prec;

  va_start(ap, fmt);
  while (*p) {
    if (*p != '%') {
      uart_SendByte((uint8_t)*p++);
      continue;
    }
    p++;                                   /* 跳过 '%' */
    if (*p == '%') {
      uart_SendByte('%');
      p++;
      continue;
    }
    prec = 2;
    if (*p == '.') {                       /* %.Nf 精度 */
      p++;
      if (*p >= '0' && *p <= '4') {
        prec = *p - '0';
        p++;
      }
    }
    switch (*p) {
      case 's': uart_Puts(va_arg(ap, const char *)); break;
      case 'c': uart_SendByte((uint8_t)va_arg(ap, int)); break;
      case 'd': uart_PutInt(va_arg(ap, int)); break;
      case 'u': uart_PutUInt(va_arg(ap, unsigned int)); break;
      case 'x': uart_PutHex(va_arg(ap, unsigned int)); break;
      case 'f': uart_PutFloat((float)va_arg(ap, double), prec); break; /* float 会提升为 double */
      default:  uart_SendByte('%'); uart_SendByte((uint8_t)*p); break;
    }
    if (*p) p++;
  }
  va_end(ap);
}

/* ------------------------- 接收中断 ------------------------- */
/* 先读 SR 再读 DR：同时清除 RXNE/ORE，防止溢出锁死 */
void UART_RxISR(void)
{
  uint8_t b;
  uint16_t next;

  (void)huart1.Instance->SR;
  b = (uint8_t)(huart1.Instance->DR & 0xFF);
  next = (uint16_t)((s_rx_head + 1) % UART_RX_BUF_SIZE);
  if (next != s_rx_tail) {
    s_rx_ring[s_rx_head] = b;
    s_rx_head = next;
  }
}

/* ------------------------- 命令解析 ------------------------- */

/* 简易字符串转浮点（支持正负号与小数点） */
static float parse_float(const char *s)
{
  float sign = 1.0f, ip = 0.0f, fp = 0.0f, scale = 0.1f;
  int i = 0;

  if (s[0] == '-') { sign = -1.0f; i++; }
  else if (s[0] == '+') { i++; }
  while (s[i] >= '0' && s[i] <= '9') {
    ip = ip * 10.0f + (float)(s[i] - '0');
    i++;
  }
  if (s[i] == '.') {
    i++;
    while (s[i] >= '0' && s[i] <= '9') {
      fp += (float)(s[i] - '0') * scale;
      scale *= 0.1f;
      i++;
    }
  }
  return sign * (ip + fp);
}

static void Cmd_Status(void)
{
  uart_Printf("OK on=%d fault=0x%x vset=%.3f vout=%.3f ilim=%.3f iout=%.3f vin=%.2f duty=%.1f%% mode=%s\r\n",
              (int)Control_IsEnabled(), (unsigned int)g_fault,
              Control_GetVset(), ADC_GetVout(),
              Control_GetIlimit(), ADC_GetIout(),
              ADC_GetVin(), PWM_GetDuty() * 100.0f,
              Control_GetMode() == MODE_CC ? "CC" : "CV");
}

static void Cmd_Set(char **t, int n)
{
  float v;

  if (n < 3) {
    uart_Printf("ERROR usage: set v|i|ocp <value>\r\n");
    return;
  }
  if (strcmp(t[1], "v") == 0) {
    v = parse_float(t[2]);
    if (v < VSET_MIN || v > VSET_MAX) {
      uart_Printf("ERROR v out of range (%.1f~%.1f)\r\n", VSET_MIN, VSET_MAX);
      return;
    }
    Control_SetVset(v);
    uart_Printf("OK vset=%.3f\r\n", Control_GetVset());
  } else if (strcmp(t[1], "i") == 0) {
    v = parse_float(t[2]);
    if (v < 0.1f || v > ILIMIT_MAX) {
      uart_Printf("ERROR i out of range (0.1~%.1f)\r\n", ILIMIT_MAX);
      return;
    }
    Control_SetIlimit(v);
    uart_Printf("OK ilim=%.3f\r\n", Control_GetIlimit());
  } else if (strcmp(t[1], "ocp") == 0) {
    v = parse_float(t[2]);
    if (v < 0.5f || v > 6.0f) {
      uart_Printf("ERROR ocp out of range (0.5~6.0)\r\n");
      return;
    }
    Protect_SetOcp(v);
    uart_Printf("OK ocp=%.3f\r\n", Protect_GetOcp());
  } else {
    uart_Printf("ERROR unknown param '%s'\r\n", t[1]);
  }
}

static void Cmd_Get(char **t, int n)
{
  if (n < 2) {
    uart_Printf("ERROR usage: get v|i|vin|duty|mode|status\r\n");
    return;
  }
  if (strcmp(t[1], "v") == 0) {
    uart_Printf("OK vset=%.3f vout=%.3f\r\n", Control_GetVset(), ADC_GetVout());
  } else if (strcmp(t[1], "i") == 0) {
    uart_Printf("OK ilim=%.3f iout=%.3f\r\n", Control_GetIlimit(), ADC_GetIout());
  } else if (strcmp(t[1], "vin") == 0) {
    uart_Printf("OK vin=%.2f\r\n", ADC_GetVin());
  } else if (strcmp(t[1], "duty") == 0) {
    uart_Printf("OK duty=%.1f%%\r\n", PWM_GetDuty() * 100.0f);
  } else if (strcmp(t[1], "mode") == 0) {
    uart_Printf("OK mode=%s\r\n", Control_GetMode() == MODE_CC ? "CC" : "CV");
  } else if (strcmp(t[1], "status") == 0) {
    Cmd_Status();
  } else {
    uart_Printf("ERROR unknown get '%s'\r\n", t[1]);
  }
}

/* pid [v|i] p <kp> i <ki> d <kd>  —— 缺省目标为电流内环；参数可部分修改 */
static void Cmd_Pid(char **t, int n)
{
  uint8_t loop = 0xFF;
  int idx = 1;
  float kp = 0.0f, ki = 0.0f, kd = 0.0f;
  uint8_t have_p = 0, have_i = 0, have_d = 0;

  if (n > 2 && (strcmp(t[1], "v") == 0 || strcmp(t[1], "i") == 0 ||
                strcmp(t[1], "c") == 0)) {
    loop = (t[1][0] == 'v') ? 0 : 1;
    idx = 2;
  }
  if (idx >= n) {   /* 仅查询 */
    Control_GetVpid(&kp, &ki, &kd);
    uart_Printf("OK vpid kp=%.3f ki=%.3f kd=%.3f\r\n", kp, ki, kd);
    Control_GetIpid(&kp, &ki, &kd);
    uart_Printf("OK ipid kp=%.3f ki=%.3f kd=%.3f\r\n", kp, ki, kd);
    return;
  }
  while (idx + 1 < n) {
    if (strcmp(t[idx], "p") == 0)     { kp = parse_float(t[idx + 1]); have_p = 1; }
    else if (strcmp(t[idx], "i") == 0){ ki = parse_float(t[idx + 1]); have_i = 1; }
    else if (strcmp(t[idx], "d") == 0){ kd = parse_float(t[idx + 1]); have_d = 1; }
    else {
      uart_Printf("ERROR bad token '%s'\r\n", t[idx]);
      return;
    }
    idx += 2;
  }
  if (idx != n) {
    uart_Printf("ERROR usage: pid [v|i] p <kp> i <ki> d <kd>\r\n");
    return;
  }
  if (have_p && (kp < 0.0f || kp > 100.0f)) { uart_Printf("ERROR kp out of range (0~100)\r\n"); return; }
  if (have_i && (ki < 0.0f || ki > 100000.0f)) { uart_Printf("ERROR ki out of range (0~100000)\r\n"); return; }
  if (have_d && (kd < 0.0f || kd > 1000.0f)) { uart_Printf("ERROR kd out of range (0~1000)\r\n"); return; }

  if (loop == 0xFF) loop = 1;   /* 缺省：电流内环 */

  /* 未指定的参数保持原值：先读当前参数再覆盖 */
  if (loop == 0) {
    if (!have_p || !have_i || !have_d) {
      float ckp, cki, ckd;
      Control_GetVpid(&ckp, &cki, &ckd);
      if (!have_p) kp = ckp;
      if (!have_i) ki = cki;
      if (!have_d) kd = ckd;
    }
    Control_SetVpid(kp, ki, kd);
    Control_GetVpid(&kp, &ki, &kd);
    uart_Printf("OK vpid kp=%.3f ki=%.3f kd=%.3f\r\n", kp, ki, kd);
  } else {
    if (!have_p || !have_i || !have_d) {
      float ckp, cki, ckd;
      Control_GetIpid(&ckp, &cki, &ckd);
      if (!have_p) kp = ckp;
      if (!have_i) ki = cki;
      if (!have_d) kd = ckd;
    }
    Control_SetIpid(kp, ki, kd);
    Control_GetIpid(&kp, &ki, &kd);
    uart_Printf("OK ipid kp=%.3f ki=%.3f kd=%.3f\r\n", kp, ki, kd);
  }
}

static void Cmd_Mode(char **t, int n)
{
  if (n >= 2) {
    if (strcmp(t[1], "cv") == 0)        Control_SetMode(MODE_CV);
    else if (strcmp(t[1], "cc") == 0)   Control_SetMode(MODE_CC);
    else {
      uart_Printf("ERROR usage: mode cv|cc\r\n");
      return;
    }
  }
  uart_Printf("OK mode=%s\r\n", Control_GetMode() == MODE_CC ? "CC" : "CV");
}

static void Cmd_Help(void)
{
  uart_Puts("Commands:\r\n"
            "  set v <0.8-10.0>      set output voltage (V)\r\n"
            "  set i <0.1-3.5>       set current limit / CC target (A)\r\n"
            "  set ocp <0.5-6.0>     set over-current threshold (A)\r\n"
            "  get v|i|vin|duty|mode|status\r\n"
            "  pid [v|i] p <kp> i <ki> d <kd>   tune PID (default: current loop)\r\n"
            "  pid                   show PID parameters\r\n"
            "  enable | disable      output on/off\r\n"
            "  mode cv | cc          control mode\r\n"
            "  save                  save settings to flash\r\n"
            "  clear                 clear fault latch\r\n"
            "  help\r\n");
}

static void UART_HandleLine(char *line)
{
  char *t[8];
  int n = 0;
  char *p = line;

  /* 按空白切词（将空白替换为 '\0'） */
  while (*p) {
    while (*p == ' ' || *p == '\t') *p++ = '\0';
    if (*p == '\0') break;
    t[n++] = p;
    if (n >= 8) break;
    while (*p != '\0' && *p != ' ' && *p != '\t') p++;
  }
  if (n == 0) return;

  if (strcmp(t[0], "help") == 0)        Cmd_Help();
  else if (strcmp(t[0], "set") == 0)    Cmd_Set(t, n);
  else if (strcmp(t[0], "get") == 0)    Cmd_Get(t, n);
  else if (strcmp(t[0], "pid") == 0)    Cmd_Pid(t, n);
  else if (strcmp(t[0], "enable") == 0) {
    Control_Enable();
    uart_Printf("OK output enabled\r\n");
  } else if (strcmp(t[0], "disable") == 0) {
    Control_Disable();
    uart_Printf("OK output disabled\r\n");
  } else if (strcmp(t[0], "mode") == 0)     Cmd_Mode(t, n);
  else if (strcmp(t[0], "save") == 0) {
    if (Storage_SaveCurrent() == 0) uart_Printf("OK saved to flash\r\n");
    else                             uart_Printf("ERROR flash write failed\r\n");
  } else if (strcmp(t[0], "clear") == 0) {
    Protect_ClearFaults();
    uart_Printf("OK faults cleared\r\n");
  } else if (strcmp(t[0], "status") == 0)   Cmd_Status();
  else uart_Printf("ERROR unknown command '%s' (help for list)\r\n", t[0]);
}

/* ------------------------- 对外接口 ------------------------- */

void UART_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* PA9 TX / PA10 RX：复用推挽 */
  g.Pin   = GPIO_PIN_9 | GPIO_PIN_10;
  g.Mode  = GPIO_MODE_AF_PP;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &g);

  huart1.Instance          = USART1;
  huart1.Init.BaudRate     = UART_BAUDRATE;
  huart1.Init.WordLength   = UART_WORDLENGTH_8B;
  huart1.Init.StopBits     = UART_STOPBITS_1;
  huart1.Init.Parity       = UART_PARITY_NONE;
  huart1.Init.Mode         = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();

  /* 接收中断（优先级 2，低于控制环 1） */
  HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
  __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
}

void UART_Poll(void)
{
  while (s_rx_tail != s_rx_head) {
    char c = (char)s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1) % UART_RX_BUF_SIZE);

    if (c == '\r') continue;
    if (c == '\n') {
      if (s_line_len > 0) {
        s_line[s_line_len] = '\0';
        UART_HandleLine(s_line);
        s_line_len = 0;
      }
    } else if (s_line_len < UART_LINE_MAX - 1) {
      s_line[s_line_len++] = c;
    }
  }
}
