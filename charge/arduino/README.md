# 充电器主控 — Arduino 版

> 与 `firmware/`（ESP-IDF）等价的 Arduino-ESP32 实现，保留双路 RS485、状态机、u8g2 OLED 三大模块。
> 适合 Arduino IDE / arduino-cli / PlatformIO 直接编译。

## 1. 工程结构

```
arduino/
└── charger_dual_rs485/
    ├── charger_dual_rs485.ino    # 入口: setup() / loop()
    ├── app_config.h              # 引脚、UART、业务参数
    ├── rs485.h / rs485.cpp       # 双路 RS485 封装 (HardwareSerial + 手动 DE)
    ├── modbus.h / modbus.cpp     # CRC16 / 读寄存器 / 设值 / 开关机
    ├── charger_fsm.h / .cpp      # 6 态充电状态机
    ├── io_ctrl.h / io_ctrl.cpp   # LED / 蜂鸣器 / 按键
    └── ui_oled.h / ui_oled.cpp   # SSD1306 (U8g2lib)
```

## 2. 依赖库（库管理器安装）

| 库名 | 作者 | 用途 |
|------|------|------|
| **U8g2** | olikraus | SSD1306 OLED 显示 |

Arduino-ESP32 内核：≥ **v2.0.14**（推荐 3.x），目标板选 *ESP32 Dev Module* 或 *ESP32 Pico Kit*。

## 3. arduino-cli 一键编译

```bash
# 安装核心 + 库
arduino-cli core install esp32:esp32
arduino-cli lib install "U8g2"

# 编译
cd arduino
arduino-cli compile -b esp32:esp32:pico32 charger_dual_rs485

# 烧录 (替换为实际串口)
arduino-cli upload -b esp32:esp32:pico32 -p /dev/ttyUSB0 charger_dual_rs485
```

## 4. OLED 界面布局（128×64，SSD1306）

```
┌──────────────────────────────────────────────┐
│ CHG 58.4V  6.00A S2                  ← 第1行 │  y=10  充电器实时 V/A/状态低4位
│ PSU 58.6V  6.05A 0000                ← 第2行 │  y=22  电源实时 V/A/状态字
│ T   00:12:34                         ← 第3行 │  y=34  本次充电累计时长
│ ████████████████░░░░░░░░░░░░░░░      ← 进度条 │  y=40~48  根据电压估算 0~100%
│ FSM: CHARGING   62%                  ← 第5行 │  y=62  状态机当前态 + 百分比
└──────────────────────────────────────────────┘
```

字段说明：

| 行 | 区域 | 内容 | 数据来源 |
|----|------|------|----------|
| 1 | CHG | 充电器电压 / 电流 / 状态字低 4 位 | `ChargerFsm::chgInfo()` |
| 2 | PSU | 电源电压 / 电流 / 完整状态字 | `ChargerFsm::psuInfo()` |
| 3 | T   | 充电累计 HH:MM:SS（CHARGING 态计数，IDLE 态归零） | `loop()` 中 `g_tick_sec` |
| 4 | 进度条 | 边框 `drawFrame(0,40,128,8)` + 填充 `drawBox(1,41,126*pct/100,6)` | 按 48V/60V/72V 三档区间映射 |
| 5 | FSM | 状态机文字 + 百分比 | `stateName()` |

刷新策略：

- **2 Hz** 整屏全缓冲刷新（`u8g2.sendBuffer()`），由 [charger_dual_rs485.ino](file:///home/lechi/Desktop/%E5%85%85%E7%94%B5%E5%99%A8/arduino/charger_dual_rs485/charger_dual_rs485.ino) 中 500 ms 节拍触发；
- 字体：`u8g2_font_6x10_tf`（高 10 像素，等宽）；
- I²C：硬件 I²C，400 kHz，地址 `0x3C`，引脚 SDA=21 / SCL=22。

开机引导界面：

```
┌──────────────────────────────────────────────┐
│                                              │
│        Charger Dual-RS485                    │
│                                              │
│              Booting...                      │
│                                              │
└──────────────────────────────────────────────┘
```

LED / 蜂鸣器联动：

| FSM 状态 | LED | 蜂鸣器 |
|----------|-----|--------|
| IDLE     | 1 Hz 慢闪 | OFF |
| DETECTED / CONFIG | 1 Hz 慢闪 | OFF |
| CHARGING | 5 Hz 快闪 | OFF |
| FULL     | 常亮 | OFF |
| FAULT    | 10 Hz 急闪 | 长鸣 |

## 5. 引脚一览（与硬件方案保持一致）

| 功能 | GPIO |
|------|------|
| 充电器 RS485 TX/RX/DE | 17 / 16 / 4 |
| 电源 RS485 TX/RX/DE   | 27 / 26 / 14 |
| OLED I2C SDA/SCL      | 21 / 22 |
| LED / KEY / BUZZER    | 2 / 0 / 25 |
| 电池接入 / ADC        | 35 / 34 |
