# 我的自定义开发板编译说明

### 配置espidf开发环境

先从espidf的github地址下载5.4.2版本的espidf

https://github.com/espressif/esp-idf

5.4.2页面:

https://github.com/espressif/esp-idf/releases/tag/v5.4.2

下载后上传到linux系统中

先激活espidf开发环境,espidf要求使用python3.9.13
```bash
conda activate espidf_5.4_py3.9.13
```

然后cd到espidf-5.4.2目录下,使用
``` bash
bash install.sh
source export.sh
```
安装espidf,安装好之后,设置idf.py环境,然后使用下边命令配置开发板
```bash
idf.py set-target esp32s3
```
# 编译小智AI固件

从github上下载最新版的小智源码

https://github.com/78/xiaozhi-esp32

然后cd到xiaozhi-esp32目录下

在mmain/boards目录下,新建一个自已板子的目录,如ai_my_board_esp32s3N16R8来定义自已的开发板

## 目录结构

每个开发板的目录结构通常包含以下文件：

- `xxx_board.cc` - 主要的板级初始化代码，实现了板子相关的初始化和功能
- `config.h` - 板级配置文件，定义了硬件管脚映射和其他配置项
- `config.json` - 编译配置，指定目标芯片和特殊的编译选项
- `README.md` - 开发板相关的说明文档

## 定制开发板步骤

### 1. 创建新的开发板目录

首先在`boards/`目录下创建一个新的目录，例如`my-custom-board/`：

```bash
mkdir main/boards/my-custom-board
```

### 2. 创建配置文件

#### config.h

在`config.h`中定义所有的硬件配置，包括:

- 音频采样率和I2S引脚配置
- 音频编解码芯片地址和I2C引脚配置
- 按钮和LED引脚配置
- 显示屏参数和引脚配置

所有配置完成后,要在CMakeLists.txt中增加自已的开发板

```shell
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_ESP32_FENGMM521)
    set(BOARD_TYPE "ai_my_board_esp32s3N16R8")
```
添加上这个之后,才可以使用下边命令编译自已的开发板固件
```bash
python scripts/release.py ai_my_board_esp32s3N16R8
```

编译好的固件在release/v1.7.6_ai_my_board_esp32s3N16R8.zip

## 重要提示

> **警告**: 对于自定义开发板，当IO配置与原有开发板不同时，切勿直接覆盖原有开发板的配置编译固件。必须创建新的开发板类型，或者通过config.json文件中的builds配置不同的name和sdkconfig宏定义来区分。使用 `python scripts/release.py [开发板目录名字]` 来编译打包固件。
>
> 如果直接覆盖原有配置，将来OTA升级时，您的自定义固件可能会被原有开发板的标准固件覆盖，导致您的设备无法正常工作。每个开发板有唯一的标识和对应的固件升级通道，保持开发板标识的唯一性非常重要。


### 4. 创建README.md

在README.md中说明开发板的特性、硬件要求、编译和烧录步骤：


## 常见开发板组件

### 1. 显示屏

项目支持多种显示屏驱动，包括:
- ST7789 (SPI)
- ILI9341 (SPI)
- SH8601 (QSPI)
- 等...

### 2. 音频编解码器

支持的编解码器包括:
- ES8311 (常用)
- ES7210 (麦克风阵列)
- AW88298 (功放)
- 等...

### 3. 电源管理

一些开发板使用电源管理芯片:
- AXP2101
- 其他可用的PMIC

### 4. MCP设备控制

可以添加各种MCP工具，让AI能够使用:
- Speaker (扬声器控制)
- Screen (屏幕亮度调节)
- Battery (电池电量读取)
- Light (灯光控制)
- 等...

## 开发板类继承关系

- `Board` - 基础板级类
  - `WifiBoard` - Wi-Fi连接的开发板
  - `Ml307Board` - 使用4G模块的开发板
  - `DualNetworkBoard` - 支持Wi-Fi与4G网络切换的开发板

## 开发技巧

1. **参考相似的开发板**：如果您的新开发板与现有开发板有相似之处，可以参考现有实现
2. **分步调试**：先实现基础功能（如显示），再添加更复杂的功能（如音频）
3. **管脚映射**：确保在config.h中正确配置所有管脚映射
4. **检查硬件兼容性**：确认所有芯片和驱动程序的兼容性

## 可能遇到的问题

1. **显示屏不正常**：检查SPI配置、镜像设置和颜色反转设置
2. **音频无输出**：检查I2S配置、PA使能引脚和编解码器地址
3. **无法连接网络**：检查Wi-Fi凭据和网络配置
4. **无法与服务器通信**：检查MQTT或WebSocket配置

## 参考资料

- ESP-IDF 文档: https://docs.espressif.com/projects/esp-idf/
- LVGL 文档: https://docs.lvgl.io/
- ESP-SR 文档: https://github.com/espressif/esp-sr 