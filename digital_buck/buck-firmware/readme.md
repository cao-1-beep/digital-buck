# 数字降压电源（Digital Buck Converter）固件

基于 **STM32F103C8T6 + STM32 HAL 库（F1 系列）** 的数控降压 DC-DC 转换器固件。

- **输入**：12V（由 U8 MG78M12N3 稳压提供）
- **输出**：0.8V ~ 10V 可调，最大 3A
- **拓扑**：同步半桥（RT9624AZS 驱动 + WXR20N04 ×2，Q1 上管 / Q2 下管）
- **开关频率**：100kHz（TIM1 互补 PWM，死区 ≈100ns）
- **控制**：电压外环（10kHz）+ 电流内环（100kHz）双闭环增量式 PID，PID 参数可在线整定
- **保护**：过流（OCP）、过压（OVP）、欠压（UVP）、可选外部比较器保护
- **交互**：5 个按键（短按/长按/连按）、USART1 文本命令、状态 LED、Flash 掉电保存参数

---

## 1. 文件结构

```
buck-firmware/
├── Core/
│   ├── Inc/
│   │   ├── main.h                主头文件（LED、遥测配置）
│   │   ├── pwm.h                 PWM 配置（频率/死区/引脚）
│   │   ├── adc.h                 ADC 配置（分压比/增益/采样时间）
│   │   ├── pid.h                 PID 与控制状态（模式/限幅/软启动）
│   │   ├── uart.h                串口配置（波特率）
│   │   ├── key.h                 按键配置（引脚/时序）
│   │   ├── protect.h             保护配置（阈值/去抖/外部比较器）
│   │   ├── storage.h             存储配置（Flash 地址/出厂默认值）
│   │   ├── stm32f1xx_it.h        中断声明
│   │   └── stm32f1xx_hal_conf.h  HAL 库配置（模块选择/HSE 值）
│   └── Src/
│       ├── main.c                初始化、主循环、时钟、按键事件、LED
│       ├── pwm.c                 TIM1 互补 PWM + 死区 + 占空比更新
│       ├── adc.c                 ADC1 三通道扫描 + DMA 环形缓冲
│       ├── pid.c                 双闭环 PID、软启动、控制周期任务
│       ├── uart.c                串口中断接收、命令解析、轻量 printf
│       ├── key.c                 按键状态机（消抖/短按/长按/连按）
│       ├── protect.c             故障检测与锁存
│       ├── storage.c             Flash 模拟 EEPROM（参数保存/加载）
│       └── stm32f1xx_it.c        中断服务函数
└── readme.md
```

> 本工程**不依赖 CubeMX 生成代码**：所有外设初始化都在模块内完成。
> 若你习惯用 CubeMX 建工程，可参考第 7 节的方式移植。

---

## 2. 引脚定义（默认，全部可用宏修改）

| 功能 | 引脚 | 宏（所在头文件） | 说明 |
|---|---|---|---|
| 上管 PWM | PA8 | `PWM_HIGH_PORT/PIN`（pwm.h） | TIM1_CH1 |
| 下管 PWM | PB13 | `PWM_LOW_PORT/PIN`（pwm.h） | TIM1_CH1N 互补输出，硬件死区 |
| 输出电压反馈 | PA0 | `ADC_VOUT_PIN`（adc.h） | ADC1_IN0，分压后采样 |
| 输出电流反馈 | PA1 | `ADC_IOUT_PIN`（adc.h） | ADC1_IN1，10mΩ 采样电阻经 U5 放大 |
| 输入电压检测 | PA2 | `ADC_VIN_PIN`（adc.h） | ADC1_IN2 |
| S1 电压 +0.1V | PB0 | `KEY1_PORT/PIN`（key.h） | 长按 +0.5V，长按后连按 +0.1V |
| S2 电压 -0.1V | PB1 | `KEY2_PORT/PIN`（key.h） | 同上 |
| S3 开关机 | PB2 | `KEY3_PORT/PIN`（key.h） | 长按清除故障 |
| S4 模式切换 | PB3 | `KEY4_PORT/PIN`（key.h） | CV/CC，长按打印状态 |
| S5 确认/保存 | PB4 | `KEY5_PORT/PIN`（key.h） | 长按恢复出厂默认 |
| USART1 TX | PA9 | — | 115200-8N1 |
| USART1 RX | PA10 | — | 中断接收 |
| 状态 LED | PC13 | `LED_PORT/PIN`（main.h） | 灭=关，亮=开，2Hz 闪=故障 |
| 比较器（可选） | PA3/PA4 | `OCP_CMP_* / OVP_CMP_*`（protect.h） | 默认关闭 |

**注意**：S4/S5 使用 PB3/PB4，这两脚默认被 JTAG 占用。固件启动时会自动
关闭 JTAG（`SWJ_CFG=010`，仅保留 SWD），ST-Link 的 SWD 烧录不受影响。
若你必须使用 JTAG 调试，请把 S4/S5 换到其他引脚并将 `KEY_NEED_JTAG_REMAP` 置 0。

---

## 3. 关键硬件参数与换算公式（改硬件只需改宏）

| 参数 | 默认值 | 宏 | 说明 |
|---|---|---|---|
| 开关频率 | 100kHz | `PWM_FREQ_HZ` | ARR=719，占空比分辨率 ≈0.139% |
| 死区时间 | 100ns | `PWM_DEADTIME_NS` | DTG=7 → 7×13.89ns ≈97ns |
| 最大占空比 | 0.95 | `PWM_MAX_DUTY` | 防止互补通道近 100% 异常 |
| 输出电压分压比 | 0.25 | `ADC_VOUT_DIVIDER` | 10k+30k 分压：10V→2.5V |
| 输入电压分压比 | 0.25 | `ADC_VIN_DIVIDER` | 10k+30k：12V→3.0V |
| 采样电阻 | 10mΩ | `ADC_RSHUNT_OHM` | R11 |
| 电流放大增益 | 50 | `ADC_AMP_GAIN` | U5(HT358VRZ)，1.5V ↔ 3.0A |
| ADC 采样时间 | 13.5cyc | `ADC_SAMPLE_TIME` | 3 通道一轮 ≈6.5µs |

换算公式（代码中已按宏自动计算，无需手工换算）：

```
Vout = ADC_raw × VREF / 4096 / 分压比
Iout = ADC_raw × VREF / 4096 / (Rshunt × Gain)
Vin  = ADC_raw × VREF / 4096 / 分压比
```

**纹波核算**（L1=68µH，CCM 模式）：

```
ΔI_L = Vout × (1 − D) / (f × L)
```

- f=100kHz、Vout=5V、D≈0.42：ΔI_L ≈ 0.43A（3A 的 14%，CCM 良好）
- f=200kHz：ΔI_L 减半；若追求占空比分辨率，也可用 72kHz（ARR=999，分辨率 0.1%）

**占空比精度说明**：72MHz 定时器时钟下，100kHz 对应 ARR=719，
分辨率 1/720 ≈ 0.139%（约 0.1%）。若需严格 0.1%，把 `PWM_FREQ_HZ` 改为 72000
（ARR=999，分辨率 0.1001%）。

---

## 4. 系统设计说明

### 4.1 控制结构

```
        VSET ──►┌──────────┐ Icmd ┌──────────┐ Duty ┌────────┐
                │ 电压外环 │─────►│ 电流内环 │─────►│ TIM1   │──► 半桥
                │ 10kHz PID│      │100kHz PID│      │ 互补PWM│
                └────▲─────┘      └────▲─────┘      └────────┘
                     │ Vout            │ Iout
                     └─────────────────┴────── ADC1 DMA（连续扫描，4轮平均）
```

- **电流内环**（100kHz，与开关频率同步，在 TIM1 更新中断中执行）：
  输入 Iout，输出占空比。响应快，限制电感电流，天然抑制过流。
- **电压外环**（10kHz，每 10 个控制周期执行一次）：输入 Vout，输出**电流指令**
  Icmd（限幅 0 ~ Ilimit）。级联结构使 CV 模式同时具备电流限制能力：
  负载过重时 Icmd 饱和，系统自动转为限流输出。
- **CC 模式**：电压环旁路，电流环直接跟踪设定电流（`set i`）。

### 4.2 PID 算法（增量式）

```
Δu(k) = Kp·[e(k) − e(k−1)]                   比例
      + Ki·Ts·e(k)                           积分（Ki 单位：1/s）
      + (Kd/Ts)·[e(k) − 2e(k−1) + e(k−2)]    微分
u(k)  = u(k−1) + Δu(k)                       输出限幅 = 抗积分饱和
```

- Ts：电流环 = PWM 周期（10µs）；电压环 = 10 × 10µs = 100µs
- 初始参数（`storage.h` 中 `STORAGE_DEFAULT_*`，可在线修改后 `save`）：

| 环路 | Kp | Ki | Kd | 单位 |
|---|---|---|---|---|
| 电压环 | 0.5 | 80 | 0 | A/V；1/s |
| 电流环 | 0.1 | 300 | 0 | 占空比/A；1/s |

### 4.3 软启动

使能输出后，**占空比上限**从 0 线性升至 0.95，用时 50ms（`SOFT_START_MS`）。
避免启动冲击电流，同时配合 OCP 保护短路过载。

### 4.4 保护逻辑

| 保护 | 触发条件 | 动作 | 恢复 |
|---|---|---|---|
| 过流 OCP | Iout > 阈值（默认 3.5A，`set ocp` 可调），连续 3 拍 | 锁断 PWM | 清除故障后重新使能 |
| 过压 OVP | Vout > VSET×1.1 或 Vout > 10.8V，连续 3 拍 | 锁断 PWM | 同上 |
| 欠压 UVP | Vin < 10V，连续 50 拍（可选，`ENABLE_UVP`） | 锁断 PWM | Vin 恢复到 10.5V 自动清除该位（输出仍需手动开启） |
| 外部比较器 | 硬件比较器输出触发 EXTI（可选，默认关闭） | 立即锁断 | 同上 |

故障锁存期间 LED 2Hz 闪烁、串口打印 `FAULT 0x..`。清除方式：
`clear` 命令、S3 长按、或重新 `enable`（enable 会自动清故障）。

### 4.5 参数存储（Flash 模拟 EEPROM）

- 位置：片内 Flash **最后一页** `0x0800FC00`（F103C8T6 为 64KB、1KB/页）
- 内容：VSET、ILIMIT、OCP、模式、双环 PID 参数，结构 + 异或校验
- 触发：S5 短按或 `save` 命令；上电自动加载，无效则使用出厂默认值
- 写入前比较内容，无变化则跳过擦写（省 Flash 寿命）
- 擦写期间（约 20ms）CPU 取指被 Flash 忙等待阻塞，PWM 保持最近占空比，
  属正常现象；若需极致安全可在保存前 `disable` 输出

---

## 5. 串口协议（115200-8N1，文本命令，回车换行结束）

| 命令 | 说明 | 示例响应 |
|---|---|---|
| `set v 5.0` | 设定输出电压（0.8~10.0V） | `OK vset=5.000` |
| `set i 2.5` | 设定电流限制/恒流目标（0.1~3.5A） | `OK ilim=2.500` |
| `set ocp 3.5` | 设定过流阈值（0.5~6.0A） | `OK ocp=3.500` |
| `get v` | 查询电压设定与实测 | `OK vset=5.000 vout=4.987` |
| `get i` | 查询电流限制与实测 | `OK ilim=3.000 iout=0.812` |
| `get vin` | 查询输入电压 | `OK vin=12.03` |
| `get duty` | 查询当前占空比 | `OK duty=41.7%` |
| `get mode` | 查询工作模式 | `OK mode=CV` |
| `get status` | 查询全部状态 | `OK on=1 fault=0x0 ...` |
| `pid p 1.5 i 0.2 d 0.01` | 修改电流环 PID（缺省目标=内环） | `OK ipid kp=1.500 ...` |
| `pid v p 1.0 i 100 d 0.0` | 修改电压环 PID | `OK vpid kp=1.000 ...` |
| `pid` | 查询双环 PID 参数 | 两行 `OK vpid/ipid ...` |
| `enable` / `disable` | 使能/禁止输出 | `OK output enabled` |
| `mode cv` / `mode cc` | 切换恒压/恒流 | `OK mode=CC` |
| `save` | 保存全部参数到 Flash | `OK saved to flash` |
| `clear` | 清除故障锁存 | `OK faults cleared` |
| `status` | 同 `get status` | — |
| `help` | 打印命令列表 | — |

- `pid` 命令支持部分参数：未指定的保持原值；`pid p 1.5 i 0.2 d 0.01`
  只改电流环（与需求文档示例一致）。
- 遥测：每 1 秒自动输出一行 `T vout=.. iout=.. vin=.. duty=.. mode=.. on=.. fault=..`
  （`UART_TELEMETRY_MS` 可关闭）。
- 故障时主动上报：`FAULT 0x3 (OCP OVP) output disabled`。

---

## 6. 按键操作

| 按键 | 短按 | 长按（800ms） | 长按后连按 |
|---|---|---|---|
| S1 | 电压 +0.1V | 电压 +0.5V | +0.1V/150ms |
| S2 | 电压 -0.1V | 电压 -0.5V | -0.1V/150ms |
| S3 | 开关机 | 清除故障 | — |
| S4 | CV/CC 切换 | 打印状态 | — |
| S5 | 保存参数到 Flash | 恢复出厂默认（需再短按保存） | — |

---

## 7. 编译与烧录

### 7.1 方案 A：直接建工程（推荐，无需 CubeMX）

本工程自带全部初始化代码和 `stm32f1xx_hal_conf.h`，只需搭建一个空工程：

**STM32CubeIDE：**
1. File → New → STM32 Project，芯片选 `STM32F103C8Tx`，**不配置任何外设**，
   直接生成（若提示下载固件包则等待完成）；
2. 删除生成的 `Core/Src/main.c`、`Core/Src/stm32f1xx_it.c` 等文件，
   把本工程的 `Core/Inc`、`Core/Src` 下所有文件加入工程；
3. 工程设置：C/C++ Build → Settings → MCU Compiler → Preprocessor：
   添加 `USE_HAL_DRIVER` 和 `STM32F103x8`；
4. 确认链接脚本（`STM32F103C8Tx_FLASH.ld`）Flash 大小 64KB；
5. 编译下载（ST-Link 默认 SWD 即可）。

**Keil MDK：**
1. 新建工程，Device 选 `STM32F103C8`，勾选 `Use MicroLIB`（可选）；
2. 将本工程所有 `.c` 加入 Source Group，头文件路径加入 Include Paths；
3. 加入 HAL 源码（来自 STM32CubeF1 固件包或 Keil 器件包）：
   `stm32f1xx_hal.c、hal_rcc.c、hal_rcc_ex.c、hal_gpio.c、hal_gpio_ex.c、
   hal_dma.c、hal_cortex.c、hal_adc.c、hal_adc_ex.c、hal_flash.c、hal_flash_ex.c、
   hal_pwr.c、hal_tim.c、hal_tim_ex.c、hal_uart.c`；
4. 加入启动文件 `startup_stm32f103xb.s`；
5. C/C++ 选项卡 Define：`USE_HAL_DRIVER,STM32F103x8`；
6. 编译烧录。

### 7.2 方案 B：CubeMX 生成工程

1. CubeMX 建 STM32F103C8Tx 工程，时钟配到 72MHz（HSE 8MHz ×9）；
2. 删除其生成的 `main.c`、`stm32f1xx_it.c`、`stm32f1xx_hal_conf.h`，
   替换为本工程的同名文件（或把本工程各模块 .c 加入，保留 CubeMX 的
   `main.c` 但删除其中重复的外设初始化）；
3. 预定义宏与方案 A 相同。

### 7.3 烧录

- ST-Link（SWD）：CubeIDE/Keil 直接下载；若之前用过 JTAG 烧录，
  首次需先擦除芯片再烧录（我们的代码会关闭 JTAG）；
- 串口 ISP：BOOT0=1 上电，用 FlyMcu 等工具烧录；
- 串口调试：115200-8N1，接 PA9/PA10 的 USB-TTL（注意共地）。

---

## 8. PID 整定指南

整定顺序：**先内环（电流），后外环（电压）**。

1. 电流环：`disable` 输出，`pid i p 0.05 i 100 d 0` 起步，`enable` 后带
   电阻负载观察阶跃响应：
   - 输出振荡（电流声/电压纹波大）→ 减小 Kp、Ki；
   - 响应太慢 → 增大 Kp、Ki（每次调 1.5~2 倍）；
   - 电流有稳态偏差 → 增大 Ki；
2. 电压环：内环调好后，`pid v p 0.3 i 50 d 0` 起步，同样观察阶跃：
   - 超调大/振荡 → 减小 Kp、Ki；
   - 静态误差 → 增大 Ki；
   - 需要更快动态 → 适当增大 Kp（注意不要引入振荡）；
3. 参数满意后 `save` 写入 Flash。

> 注意：F103 无 FPU，100kHz 控制环使用软件浮点，中断占用约数 µs，
> 属于正常设计。若需要更低 CPU 占用，可将控制环改为定点（Q 格式）运算。

---

## 9. 常见问题（FAQ）

| 现象 | 原因/解决 |
|---|---|
| 烧录失败/检测不到芯片 | 固件已关闭 JTAG：用 SWD 模式连接，必要时先全片擦除 |
| PWM 无输出 | 检查 `enable` 状态；检查 PA8/PB13 焊接与驱动使能；检查死区配置 |
| 电压偏差大 | 核对 `ADC_VOUT_DIVIDER`、`ADC_AMP_GAIN` 与实际硬件一致 |
| 输出振荡 | PID 参数过大，按第 8 节调小；检查采样滤波（`ADC_AVG`） |
| 一上电就 OCP | 采样增益标定偏差；软启动后仍有问题则检查电流采样链路 |
| ADC 读数跳动 | 增大 `ADC_SAMPLE_TIME` 或 `ADC_AVG`；检查采样电阻走线 |
| 掉电后参数丢失 | 确认未用最后一页 Flash 存代码；检查 `STORAGE_FLASH_ADDR` |
| 想用 JTAG 调试 | 将 S4/S5 移到其他引脚，`KEY_NEED_JTAG_REMAP` 置 0 |

---

## 10. 安全提醒

- 首次上电建议**串限流电阻或电子负载**调试，示波器探头确认死区后再接真实负载；
- 修改任何硬件参数前先核对原理图，特别是分压比与电流增益；
- 不要超过 MOSFET/电感/电容的耐压电流规格；散热按 3A 输出设计；
- 本固件为教学/参考实现，用于实际产品前请补充硬件保护电路与完整测试。
