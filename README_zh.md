[English](README.md) | 简体中文

加入 Discord 社区：https://discord.gg/uGUKaD2u8Z

# 面向 FPV 社区的开源图传（VTx）固件

OpenVTx 旨在提供同时支持 MSP、SmartAudio 和 Tramp 协议的图传固件。飞控端可使用其中任意一种协议，图传上电后会自动识别当前使用的协议。

从 OpenVTx 0.2 和 Betaflight 4.4 开始，推荐使用 MSP 协议。

目前 SmartAudio 已完整实现并按照 [Rev. 09](https://www.team-blacksheep.com/tbs_smartaudio_rev09.pdf) 规范进行测试。由于 IMRC 尚未公开发布 Tramp 协议标准，当前 Tramp 实现基于 Betaflight 源码中的协议逻辑。

# 功能特性

**MSP 控制** —— MSP 是最通用且厂商中立的通信协议。与 SmartAudio 和 TRAMP 一样，MSP 可用于设置图传频点、功率和 Pit 模式，并额外支持自动配置 Betaflight 中的图传表（VTx Table）。图传上电后会向飞控查询 VTx Table，如果不存在或配置不正确，OpenVTx 会自动下发正确的 VTx Table 到飞控。

**竞速模式（Race Mode）** —— 在比赛现场搭配 Betaflight 开关使用 Pit 模式时，可选择 `RCE` 图传功率档位，强制上电进入 Pit 模式并仅输出 25mW。

# LED 指示灯说明

### 正常模式

`红灯（Red）` 上电常亮，保存配置时会闪烁；在使用 MSP、SmartAudio 或板载按键时，`绿灯（Green）` 和 `蓝灯（Blue）` 用于指示当前的工作状态（频段 / 频道 / 功率）。由于 TRAMP 没有固定的 VTx Table，该指示方式不适用于 TRAMP。

VTx 状态 LED 指示逻辑如下：

<img src="img/vtxModeIndication.png" width="100%">

当使用 TRAMP 或编译选项设置为 `LED_INDICATION_OF_VTX_MODE=0` 时，`红灯` 上电常亮并在保存更改时闪烁；`绿灯` 在检测到通信协议（例如与飞控成功建立 TRAMP 通信）后点亮；`蓝灯` 每收到一个通信数据包闪烁一次。

### 错误模式

当 OpenVTx 检测到硬件错误时，`红灯（Red）` 会按以下错误码闪烁：

- 未检测到 RTC6705：长亮 1 秒表示错误码开始，随后快速闪烁 3 次。

# Betaflight 配置

从 Betaflight 4.4 起推荐使用 MSP 协议。将图传的单根遥测控制线像往常一样接到飞控某个 UART 的 TX 焊盘，然后在 Betaflight 的预设（Presets）中选择 OpenVTx Preset 并指定对应的 UART 即可，无需手动在端口（Ports）页面配置。上电后图传会自动检查并写入 VTx Table。

如果使用 SmartAudio 或 TRAMP，按常规方式在端口（Ports）页面配置对应串口，再在 OpenVTx Preset 中选择 SmartAudio/TRAMP 选项即可。

<img src="img/BF_Preset_1.png" width="40%"><img src="img/BF_Preset_2.png" width="40%">

# 固件更新

可使用 Chrome 浏览器访问在线配置工具进行升级：https://openvtx.org/ 。

只要图传已连接飞控并通过 MSP/SA/TRAMP 正常通信，即可在线升级。对于刚刷入 Bootloader 的全新 DIY 硬件，如果只刷了 Bootloader 尚未刷固件，飞控需先配置为 SmartAudio（SA），因为 Bootloader 默认使用 SA 协议波特率通信。

- 首先关闭所有飞控地面站（如 Betaflight Configurator），避免占用串口导致升级被阻塞。
- 给飞控和图传上电。
- 通过 USB 连接飞控到电脑。
- 在网页端选择版本（Version）、目标硬件（Target），点击 Flash 开始刷写。

## ST-Link 一次性烧录（Bootloader + 固件）

针对 DIY 硬件或首次刷机的 GD32F130 图传，现在**无需**再先到 `OpenVTx_bootloader` 仓库刷一次 Bootloader、再接飞控通过 UART Passthrough 刷固件。本仓库已内置预编译 Bootloader（`src/bootloader/Generic_GD32F130_bootloader.bin`）以及 PlatformIO 自动合并/烧录脚本（`src/scripts/merge_bootloader.py`），可直接通过 ST-Link 一次性烧录完整镜像，且后续仍然完全支持 UART Passthrough 升级。

### Flash 空间分配（GD32F130G6 - 32 KB）

| 区域 | 地址范围 | 大小 | 说明 |
| --- | --- | --- | --- |
| Bootloader | `0x08000000` – `0x08001FFF` | 8 KB | 内置 `Generic_GD32F130_bootloader.bin`（不足 8 KB 部分补 `0xFF`） |
| 主固件（App） | `0x08002000` – `0x08007BFF` | 23 KB | 编译时带 `-Wl,--defsym=FLASH_APP_OFFSET=8K` 偏移 |
| 模拟 EEPROM | `0x08007C00` – `0x08007FFF` | 1 KB | 用于保存图传参数 |

### PlatformIO 使用方法（`src/platformio.ini`）

1. **空白芯片或已解锁芯片（如自制 DIY 图传）**：在 PlatformIO 中选择 `DIY_VTX820_stlink` 或 `Generic_GD32F130_stlink` 环境，点击 **Upload** 即可一键烧录 Bootloader + 固件（命令行：`pio run -d src -e DIY_VTX820_stlink -t upload`）。
2. **带读保护（RDP）的成品图传（如全新 EWRF E7082VM 首次刷机）**：选择 `DIY_VTX820_stlink_unlock` 或 `Generic_GD32F130_stlink_unlock` 环境点击 **Upload**，会自动先解锁 Flash 读保护并全片擦除，再烧录合并镜像。
3. **单文件合并固件**：每次编译 `*_stlink` 环境都会自动在 `src/.pio/build/<env>/firmware_combined.bin` 生成完整的合并固件，也可直接使用 STM32CubeProgrammer、ST-Link Utility 或脱机烧录器从地址 `0x08000000` 烧录。
4. **后续 UART Passthrough 升级**：首次通过 ST-Link 烧录完成后，后续更新固件直接选择原来的正式环境（如 `DIY_VTX820`、`Generic_GD32F130`）或使用 https://openvtx.org/ 通过飞控串口透传升级即可，不会覆盖 Bootloader。

> **提示：** 如需为 `src/platformio.ini` 中的其他 GD32F130 目标添加一键 ST-Link 环境，请务必继承带 8 KB 偏移的正式环境（切勿继承 `*_develop`）：
> ```ini
> [env:<目标名>_stlink]
> extends = env:<目标名>, env_gd32f130_stlink
> ```

# 当前支持的图传硬件

OpenVTx 持续增加对更多硬件的支持，最新支持列表请查看 [src/platformio.ini](src/platformio.ini) 文件。

# DIY 制作与刷写 EWRF E7082VM

fubarphill 在 YouTube 上制作了非常详细的项目介绍与刷机教程视频，建议观看参考。刷写需要使用 [ST-LINK V2](https://www.aliexpress.com/wholesale?catId=0&initiative_id=SB_20210125211035&SearchText=ST-LINK+V2)，接线方式如下图所示。

[EWRF E7082VM V1 & V2](https://www.aliexpress.com/wholesale?catId=0&initiative_id=SB_20210125211126&SearchText=EWRF+E7082VM) —— 最大输出功率可达 [+500mW](img/EWRF_E7082VM_500mW.jpg)。

OpenVTx 使用 VS Code 和 [PlatformIO](https://platformio.org/platformio-ide) 进行开发编译。

[![open vtx flashing guide](https://i.ytimg.com/vi/JsJOMwu4hBM/hqdefault.jpg?sqp=-oaymwEcCPYBEIoBSFXyq4qpAw4IARUAAIhCGAFwAcABBg==&amp;rs=AOn4CLA1-gKpZdeOFu6gtONHsG5YoIeSzg)](https://www.youtube.com/watch?v=JsJOMwu4hBM) <img src="img/st_link_connection.png" width="20%">

# 致谢与贡献者

非常感谢 cruwaller 为 EWRF E7082VM 添加了 GD32 MCU 支持，以及 [所有其他贡献者](https://github.com/OpenVTx/OpenVTx/graphs/contributors)！
