# SPDIF 输出

新增 DDC EDID：I2C1 从设备 `0x50`，GPIO6=SDA、GPIO7=SCL，原样内置用户提供的
256 字节 EDID；接线、电平转换和读取方式见 [DDC/EDID 说明](CEC_ARC.md#ddc--edid-从设备)。

`arc-tx-eac3` 分支默认启用 **CEC TV / ARC TX**：SPDIF 改为 GPIO8，CEC 为
**GPIO18（Pico 物理脚 24，不是物理脚 18）**，默认 `PICODAC_OUTPUT=SPDIF`，
关闭占用 GPIO18 的 I2S。接线、ARC 开关、Soundbar 音量及测试固件见 [CEC/ARC 说明](CEC_ARC.md)。
本分支的新 CEC 固件尚未经过实机验证；下文实测记录属于此前无 CEC 的版本。

原 **I2S + SPDIF 双输出**仍可通过 `PICODAC_CEC=OFF`、`PICODAC_OUTPUT=BOTH` 构建。
PCM 同时输出；AC-3/DTS 透传只送到 SPDIF，I2S 保持时钟并发送零样本。
也可选择 `SPDIF` 或 `I2S` 单输出模式。

新增可选 `PICODAC_INPUT=SD_EAC3` 本地播放模式：按 GPIO2/3/4/5 的 SPI0 接线
读取 TF 卡裸 48 kHz E-AC-3，封装为 192 kHz IEC 61937 载波输出。
接线、单独固件和限制见 [TF 卡播放说明](SD_EAC3.md)。下文的 USB altset 和
Windows/Linux 声卡行为适用于默认 `PICODAC_INPUT=USB` 模式。

2026-09-16：用户确认 TF 无串口日志版播放无卡顿，作为当前可用基线。
它保留诊断计时和计数逻辑，仅关闭 UART 输出；归档固件及校验值见
[TF 可用版本记录](SD_EAC3.md)。

## 当前版本：192 kHz 透传与实测结果

当前代码的 USB `bcdDevice=0x010a`。最近的修改为 altset 4、5、6、7
增加了 **192000 Hz（192 kHz）** 双声道 16 位 IEC 61937 载波支持，
PCM altset 1、2、3 仍最高支持 96 kHz。

用户已确认当前版本：

- Windows 能正常识别声卡。
- 树莓派 Linux 上可以透传 E-AC-3（Dolby Digital Plus）及 Dolby Atmos。

以上是用户实际播放链路的验证结果；播放器、接收器型号、实际 altset、载波速率
及持续播放时长尚未记录，不据此推定其他设备、Windows E-AC-3/Atmos 播放或
TrueHD Atmos 也已验证。USB 描述符仍声明 AC-3 和 DTS-I/II/III，
没有新增独立的 E-AC-3 格式；固件按原样传送主机封装的载波，不解析压缩编码。

所有非零 altset 恢复使用同一输入终端 `bTerminalLink=0x01` 和时钟源 `0x04`。
此前仅给 altset 4 增加独立终端/时钟的版本无法在 Windows 正常加载，已移除该设计。
共享时钟在 BOTH/SPDIF 构建报告五档速率；各格式通过端点包容量及运行时校验限制速率。

## 1.03 实测结果

用户确认：Linux 下使用 1.03（USB `bcdDevice=0x0103`）播放 96 kHz／24 位
双声道 PCM，此前每隔约 13 秒出现的双路杂音不再出现。
约 60 秒诊断日志中，0.52 秒之后欠载、丢帧、静音块计数不再增长，
接收队列丢包计数保持不变，I2S/SPDIF PIO 停顿及坏包计数均为零。
日志开头仍有短暂的计数增长，原因未确认；其他格式及长期稳定性未由本次测试覆盖。
修正内容和监测方法见 [音频诊断说明](AUDIO_DIAGNOSTICS.md)。

## 接线与构建

- 默认信号：**GPIO 22**（标准 Pico 的物理引脚 29）。
- I2S：DATA=GPIO 18，BCLK=GPIO 16，LRCLK=GPIO 17，接 PCM5102A 等 I2S DAC。
- 地：Pico GND 与发射模块 GND 共地。
- GPIO 输出为 3.3 V 数字逻辑信号，可接输入兼容 3.3 V 的光纤发射模块。
  发射模块供电按其规格连接。
- 同轴输出需要合适的 SPDIF 驱动、电平匹配及隔离电路；不要将 GPIO
  直接接到同轴 RCA 输入。
- 修改输出引脚时避开已使用的 GPIO，例如 UART 日志的 GPIO 0/1、板载 LED
  的 GPIO 25；标准 Pico 的 GPIO 23/24/29 也用于板上功能。

在工程目录执行：

```powershell
cmake -S . -B build -DPICODAC_CEC=OFF -DPICODAC_OUTPUT=BOTH -DPICODAC_SPDIF_PIN=22
cmake --build build -j 4
```

生成 `build/mdac_adc2.uf2`，通过 BOOTSEL 模式复制到 Pico 即可烧录。
无需额外安装或链接 pico-extras；适配的 PIO 程序已包含在工程中。

Windows 工程已配置 Pico SDK 2.3.1 和 SDK 配套 Ninja v1.13.2。
已有 `build` 配置时，可直接使用与 VS Code `Compile Project` 相同的命令：

```powershell
& "$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe" -C build
```

切回 I2S：

```powershell
cmake -S . -B build -DPICODAC_CEC=OFF -DPICODAC_OUTPUT=I2S
cmake --build build -j 4
```

I2S 默认 DATA=18、BCLK=16、LRCLK=17。选项会保存在 CMake 缓存中，切换后需重新编译。
只需 SPDIF 时设置 `-DPICODAC_OUTPUT=SPDIF`。双输出模式会检查两种接口的引脚是否冲突。

## 双输出同步

两路共用 USB 环形缓冲区中的同一份音频，音量和静音只处理一次。
SPDIF 编码前的 PCM 同时转换为 I2S 数据；不会把 SPDIF 的 BMC 编码波形直接送入 I2S。
双输出的 I2S 固定为每声道 32 位时隙（BCLK = 64 × LRCLK），16/24 位有效数据左对齐。
32 位 USB 输入在两路均保留高 24 位，让两路有效样本保持一致。
单独的 I2S 模式仍沿用原有 16/24/32 位时隙实现。

`i2s_mirror.pio` 和 SPDIF PIO 都使用每帧 256 个周期、完全相同的分频值，
并在 PIO0 上同步启动。两路 DMA 共用 192 帧块调度：先完成的一路选择下一块，
后完成的一路使用同一块；两路都读完旧块后才释放其存储空间。无新块时两路一起选用零填充。
这样不需要分别消费 USB 数据，也没有两套独立分频器取整造成的累计采样率漂移。

这里的同步指相同帧顺序和输出速率。SPDIF/I2S 协议边沿不同，外接功放和 DAC
还各有处理延迟，因此不能保证最终模拟声音严格同相；实际引脚波形和长期播放仍需硬件验证。

## 音频行为

| USB 输入 | SPDIF 输出 |
| --- | --- |
| 16 位 | 16 位有效数据，左对齐至 SPDIF 的 24 位音频区域 |
| 24 位（32 位容器） | 保留全部 24 位有效数据 |
| 32 位 | 取高 24 位，丢弃低 8 位，不加抖动 |

PCM 均支持双声道 44.1、48、88.2、96 kHz。沿用 USB 音量、主/左右声道静音和反馈端点。
采样率修改会重置输出和音频缓冲区，再重新缓冲播放。PCM 活动期间仅接受上述四档；
BOTH/SPDIF 构建在停止状态或 Type III 透传期间还接受 192000 Hz。
192 kHz 下不能直接切入 PCM，主机需先将共享时钟设回受支持的 PCM 速率。
拒绝不兼容的接口切换时保持原端点和接口状态。I2S 单输出仍只提供四档 PCM 速率。

`spdif_encode.c` 生成完整的 192 帧块，包含 B/M/W 前导码、有效性位、声道状态及偶校验。
声道状态随输入格式更新 PCM/Non-PCM、采样率和有效位数。
`spdif.c` 的单输出模式使用 PIO0 的一个状态机和一个 DMA 通道；
双输出模式使用 PIO0 的两个状态机和两个 DMA 通道，共用 DMA IRQ1，LED 仍使用 PIO1。
编码完成的缓冲区才交给 DMA；没有新块时发送完整静音块，保持块边界。
停止 USB 音频接口后关闭输出，GPIO 拉低；静止状态不维持接收器锁定。

The system clock is now 132 MHz (1584 MHz PLL VCO / 6 / 2), within the SDK PLL limits.
Both PIO serializers use 256 cycles/frame and the same rounded fractional divider.
With the current divider rounding, calculated average rate error is about +100 ppm
at 44.1/88.2 kHz and -727 ppm at nominal 192 kHz (about 191860 Hz);
48/96 kHz are exact on average. These are calculated values, not pin measurements.
Fractional-divider jitter and receiver compatibility still require hardware validation.

Periodic distortion diagnostics (cumulative debugger counters, reset on reboot):
- `audio_underrun_count`: input starvation recovery events.
- `audio_dropped_frames`: stereo frames discarded on input buffer overflow.
- `spdif_silence_block_count`: DMA blocks without a prepared audio block.
- `spdif_tx_stall_count` / `i2s_tx_stall_count`: DMA intervals with a PIO TX stall.

No diagnostic printing is added to the audio hot path. Compare counter changes during
an audible fault and record rate, depth, DAC model and whether SPDIF is affected at
the same time. The Linux 96 kHz/24-bit result above applies to firmware 1.03 as a
whole; it does not isolate the effect of the clock correction.

USB 缓冲提交已按 RP2040 并发访问要求改为两步：先写长度、PID 和 FULL，
等待至少 12 个系统时钟周期，再置 AVAILABLE。适用于音频、反馈及控制端点。
参考 [TinyUSB 的 RP2040 驱动](https://github.com/hathach/tinyusb/blob/master/src/portable/raspberrypi/rp2040/rp2040_usb.c)
中的缓冲控制寄存器写入顺序。新增主机测试检查提交前的寄存器状态；
该测试不能模拟芯片时钟域竞争；已确认的 1.03 播放结果见上文，其他条件需分别验证。

## Dolby Digital / DTS 透传

SPDIF 和 BOTH 构建提供 USB Audio Class 2.0 **Type III / IEC 61937** 格式，
用于将主机封装的压缩音轨送到支持对应格式的功放，描述符声明 AC-3 和 DTS Core 格式。
这不是 AC-3/DTS 编码器或解码器，不会把游戏或系统的多声道 PCM 实时编码成 5.1。
当前版本还已由用户在树莓派 Linux 上验证 E-AC-3 和 Dolby Atmos 透传，
但没有新增 E-AC-3 USB 格式声明，也未验证 TrueHD、DTS-HD 或所有 Atmos 载体。

| USB AudioStreaming alternate setting | 声明格式 | 支持速率（kHz） | OUT 最大包（字节） |
| --- | --- | --- | --- |
| 0 | 停止 | — | — |
| 1 | PCM 16 位 | 44.1 / 48 / 88.2 / 96 | 388 |
| 2 / 3 | PCM 24 / 32 位（32 位容器） | 44.1 / 48 / 88.2 / 96 | 776 |
| 4 | IEC 61937 AC-3（Dolby Digital） | 44.1 / 48 / 88.2 / 96 / 192 | 772 |
| 5 / 6 / 7 | IEC 61937 DTS-I / DTS-II / DTS-III | 44.1 / 48 / 88.2 / 96 / 192 | 772 |

四个 Type III 格式使用两通道、16 位、little-endian 的 IEC 61937 载波。
主机/播放器必须先封装 Pa/Pb/Pc/Pd、压缩数据和填充，再选择对应 Type III 格式。
USB 模式不对裸 `.ac3` / `.dts` 文件进行封装，也不在 PCM alternate setting 中自动检测压缩数据。
TF 模式则自行封装裸 E-AC-3，见 [SD_EAC3.md](SD_EAC3.md)。
载波时钟支持上述五档；播放时必须匹配主机封装和接收器支持的速率。
192 kHz 时每毫秒为 192 帧，端点预留 193 帧（772 字节）以容纳反馈调整。
SPDIF/BOTH 环形缓冲区的最大容量已扩至 192 kHz 所需大小，
SPDIF 声道状态也增加了对应速率标记。载波速率不等于压缩音轨的原始 PCM 采样率。
I2S 构建仅保留 PCM alternate settings，不提供压缩透传。
在 BOTH 模式下选择 AC-3/DTS 时，I2S 整块输出零样本，绝不发送压缩载波；切回 PCM 后自动恢复双输出。

透传模式会完整跳过软件音量及主/左右声道静音；这些控制值继续保留，切回 PCM 后生效。
**请在功放上调节音量和静音。** 不要对压缩载波使用播放器音量、均衡器、混音或重采样。
SPDIF 声道状态的 Non-PCM 位设为 1，有效性位 V 也设为 1，避免接收端把压缩数据当 PCM 转换。
缺数据时输出零填充并保持 Non-PCM 状态；中断的压缩突发可能无法解码，
接收器需要在后续完整突发处重新同步。固件不保证 USB 丢包或缓冲区溢出后的无缝播放。
停止接口或 USB 复位会停止旧音频，重新打开时清空缓冲区。

### 在 Windows 上使用

1. 烧录新的 `build/mdac_adc2.uf2`，重新插拔 USB，让主机重新读取描述符。
   当前代码的 USB 设备版本为 `0x010a`；若仍显示旧格式，可移除旧设备实例后重新连接。
2. SPDIF 输出接支持 AC-3/DTS 的功放或家庭影院接收器，选择对应光纤/同轴输入。
3. 使用支持 WASAPI 独占和 encoded passthrough 的播放器，在输出设置中选择本设备，
   开启 AC-3、DTS bitstream/passthrough；不要选择解码为多声道 PCM。
4. 播放已知的 AC-3 或 DTS 音轨，检查功放是否显示 Dolby Digital / DTS。
   普通 PCM 文件继续使用原有立体声模式。

Windows 内置 `usbaudio2.sys` 支持上述 Type III 格式，见
[Microsoft USB Audio 2.0 驱动文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers)。
其他系统也需由驱动实际选择 Type III alternate setting；仅把数据作为 PCM 写入 alt 1 不会开启透传。
当前版本已由用户确认 Windows 能正常识别声卡；Windows 上具体编码格式的
播放器协商和功放解码仍需单独验证，不能以正常枚举替代播放验证。

### 树莓派 Linux 实测与复测记录

用户已确认 E-AC-3 / Dolby Atmos 透传成功。复测时沿用已成功的播放器和接收器配置，
保持 encoded passthrough，避免软件混音、音量处理或重采样。
播放期间可记录以下信息，便于确认实际接口与载波速率：

```bash
cat /proc/asound/card*/stream*
```

记录运行中的 altset、采样率、播放器/系统版本和接收器格式显示。
停止状态下列出的格式只是设备能力列表，不是实际播放参数。
可结合 [音频诊断说明](AUDIO_DIAGNOSTICS.md) 观察欠载、丢帧和 PIO 停顿增量。

## 验证

主机编码测试（GCC，不依赖 Pico 硬件）：

```powershell
python tests/run_tests.py
```

编码器测试覆盖五种采样率 × 三种输入位深、PCM/Non-PCM 零填充、前导码、音频位序、
有效性、偶校验和完整声道状态块，并检查 SPDIF/I2S 两套 USB 描述符。
另外检查 I2S PIO 源码的周期数、MSB 位序和 LRCLK 延迟，比较 I2S 数据与 SPDIF
解码后的有效样本，并测试双 DMA 的块配对、存储占用和缺数据行为。
透传集成测试使用真实的 USB 音频处理、环形缓冲区和 SPDIF 编码代码，仅替换硬件接口：
输入跨包、跨块的合成 AC-3/DTS IEC 61937 突发，验证四种格式 × 五种载波时钟的逐位一致性、
192/193 帧包、音量/静音旁路、缺数据及 PCM/透传/采样率切换。
还检查单一时钟、各 altset 终端一致、包容量、时钟 GET RANGE/GET CUR、
活动 PCM 拒绝 192 kHz，以及 192 kHz 下拒绝切入 PCM。
编码器单元测试包含 192 kHz PCM 并不表示 USB PCM 开放了该速率。
合成载荷不是可供功放解码的真实音轨，也不能代替 E-AC-3/Atmos 播放验证。

硬件验收建议：

1. 连接 SPDIF DAC，逐个测试 PCM 的四档采样率和 USB 位深，确认锁定和声音正常。
2. 播放仅左/仅右声道信号，确认声道顺序；测试系统音量及静音。
3. 连续执行停止/启动、位深切换及采样率切换，检查无旧音频重放。
4. 用逻辑分析仪验证 192 帧块、输出采样率及播放中断供数据时的静音块。
5. 分别播放真实 AC-3 和 DTS Core 5.1 声道测试音轨，确认功放格式显示和声道映射；
   重复暂停/恢复和 PCM/压缩音轨切换。
6. BOTH 模式下同时观察两路输出，长时间播放 44.1/88.2 kHz PCM，确认没有累计漂移；
   AC-3/DTS 播放时检查 I2S DATA 为零、时钟保持，切回 PCM 后两路恢复。
7. 在已支持的播放链路上复测 192 kHz 载波和 E-AC-3/Atmos，记录运行参数、
   功放显示及诊断计数，并重复透传与 96 kHz 以下 PCM 的切换。

软件测试和编译不能代替实际的接收器锁定、电气和听音验证。

## 参考源码与许可

- [rpf16rj/usb_sound_card_with_pico-master](https://github.com/rpf16rj/usb_sound_card_with_pico-master)：
  参考 `streaming.cpp` 中的 IEC 61937 旁路思路和 `streaming_spdif_out_impl.h`
  中的 Non-PCM 状态设置。本工程独立实现 Type III 格式选择，不采用该工程的 PCM 同步字自动检测。
- [USB Audio Data Formats 2.0（USB-IF 文档镜像）](https://amatriz.net/PDFs/Frmts20-final.pdf)：
  §2.3.3 和 Table A-4，Type III 布局及 AC-3/DTS 格式位分配。
- [IEC 61937-1:2021 预览](https://preview.sist.si/sist-preview/101993/f9c9621694de4bdcbc934f16d281bf1d/IEC-61937-1-2021.pdf)：
  §6.1.3/6.1.4，有效性位及 Non-PCM 声道状态位。
- [pico-extras audio_spdif](https://github.com/raspberrypi/pico-extras/tree/52fd7a786ce47c989afeaee67e0c6aebee56ed2d/src/rp2_common/pico_audio_spdif)：
  参考 NRZI/BMC 编码、前导码与 PIO 方案。其音频池接口主要支持 16 位 PCM，
  本工程使用独立的 16/24/32 位输入适配器。`spdif.pio` 保留上游版权声明，
  对应许可见 [LICENSE.pico-extras](LICENSE.pico-extras)。
- [pico-sdk hardware_dma](https://github.com/raspberrypi/pico-sdk/tree/079c6f39023649b154152db30f1d781e884879bc/src/rp2_common/hardware_dma)：
  使用 SDK 的 DMA、PIO、时钟及 IRQ API；本次固件构建使用本机 SDK 2.3.1。
- [pico-examples pio/uart_dma](https://github.com/raspberrypi/pico-examples/tree/0d62f75bafc2c8120d3276c3343d1a9195e909e9/pio/uart_dma)：
  参考 PIO/DREQ 驱动 DMA、共享中断和资源清理方式。
