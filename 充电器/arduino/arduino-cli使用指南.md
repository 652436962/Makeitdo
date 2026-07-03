# arduino-cli 使用指南

## 安装

参考 [官方文档](https://arduino.github.io/arduino-cli/latest/)：

```bash
# Linux/macOS
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# 或通过包管理器
# Ubuntu: sudo apt install arduino-cli
# macOS: brew install arduino-cli
```

## 基础配置

```bash
# 创建配置目录（首次使用自动创建）
arduino-cli config init

# 添加 ESP32 板管理 URL（已有可跳过）
arduino-cli config add additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

## 板管理

```bash
# 更新板索引
arduino-cli core update-index

# 安装 ESP32 核心
arduino-cli core install esp32:esp32

# 查看已安装的核心
arduino-cli core list

# 列出可用板子
arduino-cli board listall esp32:esp32
```

## 库管理

```bash
# 安装库
arduino-cli lib install "U8g2"
arduino-cli lib install "PubSubClient"

# 搜索库
arduino-cli lib search "oled"

# 查看已安装的库
arduino-cli lib list

# 卸载库（需指定库名）
arduino-cli lib uninstall "U8g2"
```

## 编译

```bash
# 编译到默认目录（缓存目录）
arduino-cli compile -b esp32:esp32:pico32 /path/to/sketch

# 编译到指定目录
arduino-cli compile -b esp32:esp32:pico32 --output-dir ./build /path/to/sketch

# 编译并导出 bin 到 sketch 目录
arduino-cli compile -b esp32:esp32:pico32 -e /path/to/sketch
```

**常用参数：**
- `-b, --fqbn`：指定板子（如 `esp32:esp32:pico32`）
- `--output-dir`：指定输出目录
- `-e, --export-binaries`：将编译产物复制到 sketch 目录
- `--clean`：清理缓存重新编译
- `-v, --verbose`：显示详细输出
- `-q, --quiet`：静默模式（只显示错误）

## 烧录

```bash
# 列出可用串口
arduino-cli board list

# 上传到设备
arduino-cli upload -b esp32:esp32:pico32 -p /dev/ttyUSB0 /path/to/sketch

# 编译后直接上传
arduino-cli compile -b esp32:esp32:pico32 -u -p /dev/ttyUSB0 /path/to/sketch
```

**常用参数：**
- `-p, --port`：串口设备（如 `/dev/ttyUSB0`、`COM3`）
- `-b, --fqbn`：指定板子
- `-u, --upload`：编译后自动上传
- `-t, --verify`：上传后验证

## 监视器

```bash
# 打开串口监视器
arduino-cli monitor -p /dev/ttyUSB0 -b esp32:esp32:pico32

# 指定波特率
arduino-cli monitor -p /dev/ttyUSB0 -b esp32:esp32:pico32 --baud 115200
```

## 常用 FQBN 参考

| 板子 | FQBN |
|------|------|
| ESP32 Dev Module | `esp32:esp32:esp32` |
| ESP32 Pico Kit | `esp32:esp32:pico32` |
| ESP32-S3 | `esp32:esp32:esp32s3` |
| Arduino Uno | `arduino:avr:uno` |
| Arduino Mega2560 | `arduino:avr:mega` |

## 示例：完整流程

```bash
# 1. 安装核心
arduino-cli core install esp32:esp32

# 2. 安装库
arduino-cli lib install "U8g2"

# 3. 编译到 sketch 目录
arduino-cli compile -b esp32:esp32:pico32 -e /path/to/sketch

# 4. 上传
arduino-cli upload -b esp32:esp32:pico32 -p /dev/ttyUSB0 /path/to/sketch

# 5. 监视输出
arduino-cli monitor -p /dev/ttyUSB0 -b esp32:esp32:pico32 --baud 115200
```

## 故障排除

```bash
# 查看版本
arduino-cli version

# 调试编译问题
arduino-cli compile -b esp32:esp32:pico32 -v /path/to/sketch

# 清理并重新编译
arduino-cli compile -b esp32:esp32:pico32 --clean /path/to/sketch

# 查看详细错误
arduino-cli compile -b esp32:esp32:pico32 2>&1 | grep -i error
```