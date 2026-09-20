# CEC TV / ARC TX 测试版

2026-09-17，分支 `arc-tx-eac3`。新增 CEC 控制，不解码或重新编码 EAC3。
参考 [gkoh/pico-cec](https://github.com/gkoh/pico-cec) 的开漏 GPIO 和 CEC 时序思路，
此处使用独立的定时状态机，不引入其 FreeRTOS 任务系统。
ARC 消息方向参考 [Android TV 实现](https://android.googlesource.com/platform/frameworks/base/+/471eead4a327606b5b581659e04698b5ebdafd16/services/core/java/com/android/server/hdmi/HdmiCecLocalDeviceTv.java)，
操作码参考 [Linux CEC 定义](https://www.kernel.org/doc/html/v5.17/userspace-api/media/cec/cec-header.html)。

## 接线与操作

所有编号均为 **GPIO 编号**。

| 功能 | GPIO | 标准 Pico 物理脚 | 说明 |
| --- | --- | --- | --- |
| CEC | GP18 | 24 | 开漏：只拉低或释放，接外部 CEC 接口电路 |
| DDC SDA | GP6 | 9 | I2C1 从设备，接双向电平转换 |
| DDC SCL | GP7 | 10 | I2C1 时钟，由 Soundbar 主机提供 |
| SPDIF → ARC 电路 | GP8 | 11 | 接用户已设计的 SPDIF 转 ARC 电路输入 |
| TF SCLK / MOSI / MISO / CS | GP2 / 3 / 4 / 5 | 4 / 5 / 6 / 7 | SPI0，保持原接线 |
| ARC 开关按键 | GP10 | 14 | 按下接地，切换开启/关闭 |
| Soundbar 音量加 | GP26 | 31 | 按下接地，长按重复 |
| Soundbar 音量减 | GP27 | 32 | 按下接地，长按重复 |

GP18 原为 I2S DATA，因此本版禁用 I2S，要求 `PICODAC_OUTPUT=SPDIF`。
按键和 LED 不占 GP18，不需屏蔽；USB 模式下上述按键改为 CEC 控制，不再同时
向 USB 主机发送媒体按键。USB 模式禁用第三路 LED（GP26/GP28），LED 报告第三位固定为零，
其余两路保留；TF 模式不初始化 USB LED。GP13 不再控制 ARC 音量。
CEC GPIO 无内部上拉；需要外部合适的上拉、开漏接口、电平保护和共地。
不要把 GP18 接到 5V，也不要把 GP8 直接接 HDMI ARC 差分线。

## ARC 行为

设备固定作为 TV：逻辑地址 `0`，物理地址 `0.0.0.0`；Soundbar 为 Audio System，逻辑地址 `5`。
开机约 1 秒后先探测地址 0。若已有 TV 应答，本机停止注册和 ARC 输出，避免地址冲突；
因此本固件适用于 Pico 替代 TV 的独立链路，不适合与真实 TV 共用 CEC 总线。

1. TV → Soundbar：`05 70 00 00` 请求 System Audio；`05 C3` 请求 ARC 开启。
2. Soundbar → TV：`50 C0` Initiate ARC。
3. TV → Soundbar：`05 C1` Report ARC Initiated，**发送得到 ACK 后**才开启 GPIO8 载波。
4. 本地关闭：立即停止载波，发送 `05 C4` 和 `05 70`。
5. Soundbar 回复或主动发送 `50 C5` 时，停止载波并发送 `05 C2`。

开始阶段无应答会超时后重试；单帧发送最多尝试 3 次。收到明确拒绝、Standby 或
Soundbar 关闭 System Audio 后不强行重启，可用 GP10 重新请求。
本固件不把 Soundbar 拔线视为可靠可检测事件；没有 HPD 管理，未收到关闭消息时
可能仍维持已开启的载波。故障恢复可按两次 GP10 或重新上电。
音量使用 `User Control Pressed (44)` 的 `41/42` 与 `User Control Released (45)`，
长按约每 300 ms 重复。调的是 Soundbar，不改动压缩音频数据。

支持基础物理地址、OSD 名称、CEC 版本、电源状态查询及音频状态接收；
未实现完整 HDMI TV 功能、HPD/+5V 管理、厂商私有命令或 eARC。
CEC 关闭是 ARC 音频关闭，不是 Pico 断电，电源状态查询仍报告 On。
外部 HDMI 电路必须自行满足 Soundbar 建链所需条件；不能仅凭 CEC 握手保证 Atmos 兼容性。

## 已生成的测试固件

两种固件现均包含下述 DDC EDID 从设备。

- TF：`build/arc-tx-eac3/mdac_adc2.uf2`。TF 根目录 `TRACK.EC3`，裸 48 kHz EAC3；
  文件选择、格式和 FAT 限制沿用 [TF 文档](SD_EAC3.md)。不是自动寻找任意文件。
- USB：`build/arc-tx-usb/mdac_adc2.uf2`。仍作为 USB 声卡接收音频，CEC 成功后输出。
- 原可用归档 `build/releases/mdac_sd_eac3_usable_20260916.uf2` 未修改。

TF 版保留可用基线的 `PICODAC_SD_DIAGNOSTICS=ON`、`PICODAC_SD_UART_LOG=OFF`，
保留统计与计时而不输出 UART 日志。系统时钟仍为 120 MHz，192 kHz Non-PCM 载波。
ARC 未开启时不消耗 TF 播放位置；中途关闭后重新开启，从当前完整 IEC 61937 burst
的头部恢复，最多重放一个 32 ms burst。USB 模式没有暂停主机，恢复时由接收器
重新同步后续 IEC 61937 burst。

## 编译（PowerShell，SDK 自带 Ninja）

```powershell
$arcCmake = "$env:USERPROFILE/.pico-sdk/cmake/v4.3.4/bin/cmake.exe"
$arcNinja = "$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe"
& $arcCmake -S . -B build/arc-tx-eac3 -G Ninja "-DCMAKE_MAKE_PROGRAM=$arcNinja" `
  -DCMAKE_BUILD_TYPE=Release -DPICODAC_INPUT=SD_EAC3 -DPICODAC_OUTPUT=SPDIF `
  -DPICODAC_SPDIF_PIN=8 -DPICODAC_CEC=ON -DPICODAC_CEC_PIN=18 `
  -DPICODAC_DDC=ON `
  -DPICODAC_SD_DIAGNOSTICS=ON -DPICODAC_SD_UART_LOG=OFF
& $arcNinja -C build/arc-tx-eac3
```

首次配置需要下载固定版本 pico_fatfs；离线可额外指定已有源码目录：
`-DFETCHCONTENT_SOURCE_DIR_PICO_FATFS=D:/RaspberryPi-Pico/pico-demo/pico-dac2/build/reference-pico-fatfs`。
USB 构建改用 `-B build/arc-tx-usb -DPICODAC_INPUT=USB`，其他 CEC/引脚参数保持相同。
请使用独立构建目录，旧 `build` 的缓存不会自动迁移到新接线。

## 验证范围与排查

已通过主机 CEC 收发/握手/音量测试、原有 USB 与 EAC3 测试，以及两种输入的 SDK 编译。
仍需实机确认 CEC 电气波形、Soundbar 握手和连续播放稳定性；本版不能直接替代先前实测结论。
CEC 每 50 us 用短定时回调采样，消息队列/按键/握手在主循环处理；不在中断内打印或等待。
DMA 音频中断仍保持最高优先级，CEC 遇到严重计时迟到会释放总线并报错重试。

可用调试器观察：`cec_rx_frames`、`cec_tx_frames`、`cec_tx_errors`、`cec_rx_errors`、
`cec_address_conflict`、`cec_arc_state`（0 关闭、1 等待 C0、2 等待 C1 发送成功、3 开启、4 正在关闭）。
若 GPIO8 没信号，先检查是否有 `05 C3 → 50 C0 → 05 C1`，以及地址冲突和 CEC 电平。
本测试版不新增 CEC 串口日志。

## DDC / EDID 从设备

`PICODAC_DDC=ON`：I2C1，SDA=GP6、SCL=GP7，7 位地址 `0x50`
（总线地址字节写 `0xA0`、读 `0xA1`）。新构建默认随 CEC 开启；已有缓存可显式设置。
采用 SDK 的 [pico_i2c_slave 中断接口](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#group_pico_i2c_slave)，
主机提供时钟，支持硬件 clock stretching；不占用音频主循环，不输出串口日志。
初始化先于 CEC 和 TF/USB，因此尚未建立 ARC 或 TF 挂载失败时也可读取 EDID。

数据源为用户提供的 `ep-edid-retek-2000.bin`，256 字节，内置于 `ddc_edid_data.c`。
逐字节对比原文件一致；128 字节基本块与一个 CTA 扩展块的校验和均为零。
SHA-256：`0a6848a83cddc06dc2db6fe4909122351c22f186180ece805e96712161223b8c`。
不依赖桌面文件或 TF 卡，不改写能力声明和 HDMI VSDB 地址字段；它们不代表固件
已实现所有声明功能。Soundbar 是否接受该 EDID、是否会在其 ARC 端口发起读取需实测。

读取例：START → `0x50 W` → 偏移 `0x00` → repeated START → `0x50 R` → 读 128 字节 → STOP；
扩展块偏移为 `0x80`。也支持写偏移后 STOP 再读、连续读 256 字节及当前位置读取。
偏移超过 `0xFF` 回绕到 `0x00`；STOP/重启不清空读指针。每次写事务的首字节设置偏移，
后续写入字节丢弃，EDID 只读。只有 256 字节，**不响应 `0x30` E-DDC 段指针或其他地址**。

HDMI DDC 侧可能存在 5V 上拉，必须使用合适的双向开漏电平转换并共地，
Pico 侧上拉至 3.3V；固件禁用 GP6/7 内部上拉。不得将 GPIO 直接接 5V 总线。
不要让同一总线上其他 EDID EEPROM 同时响应 `0x50`。HPD 和 HDMI +5V 仍由外部电路处理。
GPIO6/7 与默认 TF GPIO2/3/4/5 不冲突；自定义引脚冲突会由 CMake 拒绝。

主机测试覆盖初始化、偏移写入、重复起始事件、分块与连续读取、回绕、写保护和校验和；
未验证真实 DDC 波形、clock stretching 兼容性及其与 CEC/音频同时运行的稳定性。
调试计数：`ddc_read_bytes`（提供给 TX FIFO 的字节数）、`ddc_offset_writes`、`ddc_ignored_writes`。
