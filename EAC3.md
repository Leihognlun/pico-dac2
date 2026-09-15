# E-AC-3 / Dolby Digital Plus 实验透传（1.10）

这是正式版 v0.2.0 的默认功能，可通过 `PICODAC_EAC3_PASSTHROUGH=OFF` 关闭。
目标是原样传送 **48 kHz E-AC-3（包括 JOC/Atmos 元数据）** 的 IEC 61937 载波。
Pico 不解码、不重新编码、不生成 Atmos；接收端必须能解码该码流。
除主机端数据路径测试和固件编译外，已完成 CM4 → Pico → 条形音响实测，
E-AC-3 杜比 Atmos 音频能够正常播放。

## 四份 UAC3 规范给出的依据

| 文档 / 页码（PDF 页号） | 结论与本次实现 |
| --- | --- |
| Frmts30 §2 / p9，§2.3.2 / p14–15 | Type III 是 IEC 61937 封装后的双声道 16 位伪 PCM；时钟报告的是传输采样率，可为源采样率整数倍 |
| Frmts30 Table A-2 / p23 | UAC3 E-AC-3 为 D21，Type III/IV；不是 UAC2 的位定义 |
| Audio30 Table 4-49 / p97–98 | UAC3 AS 描述符为 23 字节、bmFormats 为 8 字节，并使用 Cluster 描述符；不能只把当前 bInterfaceProtocol 改成 0x30 |
| BasicAudioDevice30 §4.2 / p13 | BADD 可用 Full Speed，但其基础格式固定 48 kHz、16/24 位；BADD 不能直接充当本项目的 192 kHz E-AC-3 载波模式 |
| Termt30 Table 2-6 / p9 | SPDIF 外部终端类型为 0x0605；终端声明本身不保证接收机支持 E-AC-3 |

原始文档：
[Audio30.pdf](<D:/华为家庭存储/Gush/Hardware Software/USB/UAC3.0/Initial Release/Audio30.pdf>)、
[BasicAudioDevice30.pdf](<D:/华为家庭存储/Gush/Hardware Software/USB/UAC3.0/Initial Release/BasicAudioDevice30.pdf>)、
[Frmts30.pdf](<D:/华为家庭存储/Gush/Hardware Software/USB/UAC3.0/Initial Release/Frmts30.pdf>)、
[Termt30.pdf](<D:/华为家庭存储/Gush/Hardware Software/USB/UAC3.0/Initial Release/Termt30.pdf>)。

这些资料是传输模型依据，本次没有迁移整个工程到 UAC3。
参考 CM6646，实验 alt 8 改为 UAC2 **Type III / 16 bit 双声道载波**。
AS_GENERAL 和 FORMAT_TYPE 的 bFormatType 均为 3，bmFormats 为 `0x00000381`
（AC-3、DTS-I/II/III；不复制 CM6646 的 WMA D12）。保留固定 192 kHz 时钟、
772 字节全速包及异步反馈，不照搬其高速同步端点参数。
这是 AC-3/DTS 兼容声明下的 E-AC-3 载波实验，不是标准 E-AC-3 能力声明，也不保证
桌面播放器自动出现 Dolby Digital Plus 选项。严禁在此设置发送普通 192 kHz PCM：
固件始终将其标记为 non-PCM。

## 带宽与封装

以用户提供的 48 kHz、1536 SPF、448 kb/s 文件为例：

- 每个 1536 样本周期为 32 ms；E-AC-3 IEC 61937 burst 为 24,576 字节。
- 对应 6144 个双声道 16 位载波帧，载波速率为 192 kHz。
- USB 平均负载为 `192000 × 2 × 2 = 768000 byte/s = 6.144 Mb/s`。
- 1 ms 等时包通常为 768 字节，端点预留 193 帧即 772 字节，小于 Full Speed
  单等时端点 1023 字节限制；还需在实机验证 USB 调度、编码吞吐与 PIO 时序。
- 源文件 448 kb/s 不是 USB 载波速率；JOC 的 6 声道也不是 USB 的通道数量。
- IEC 61937 `Pc & 0x1f = 0x15`；E-AC-3 的 `Pd` 是有效载荷**字节数**，不是 AC-3
  使用的位数。使用 FFmpeg 的 spdif muxer 完成封装，保留原始压缩数据。

封装依据：[FFmpeg spdifenc.c](https://ffmpeg.org/doxygen/trunk/spdifenc_8c_source.html)。
USB 限制参考：[USB 2.0 规范](https://www.usb.org/document-library/usb-20-specification)。

## 固件结构

- `PICODAC_EAC3_PASSTHROUGH=ON` 使 USB bcdDevice 为 0x0111，避免 Windows 沿用
  v0.2.0（0x0110）失败枚举留下的设备缓存。
- AS interface 1 的 alt 8：2ch / S16_LE，最大包 772，异步反馈，192000 Hz 载波。
- 参考 CM6646 的单拓扑方式，alt 1..8 全部链接 Input Terminal ID 1，并共用
  Clock Source ID 4；时钟范围增加 192000 Hz。Windows `usbaudio2.sys` 要求同一个
  AudioStreaming 接口的所有 alternate setting 使用相同 `bTerminalLink`，且只支持
  一个 Clock Source。旧版独立 Clock/Terminal 会导致 Windows Code 10。
- 环形缓冲容量扩至 192 kHz 下 16 ms（24,576 字节）；USB 包快照原有 776 字节容量足够。
- SPDIF channel status 为 non-PCM、192 kHz；保留每个载波字。USB 音量、静音不作用于载波。
- BOTH 模式的 I2S 发送零样本并维持时钟，避免把压缩数据送进模拟 DAC。
- HID 媒体键和 LED 功能保留。只支持 SPDIF/BOTH 构建，I2S-only + EAC3 在配置时拒绝。

## 构建

在已有 Pico SDK 工具链环境中：

```sh
cmake -S . -B build/eac3 -G Ninja -DPICODAC_OUTPUT=BOTH -DPICODAC_EAC3_PASSTHROUGH=ON
cmake --build build/eac3
```

实验固件：`build/eac3/mdac_adc2.uf2`。普通固件：`build/mdac_adc2.uf2`。
烧录固件后重新插拔，`lsusb -v -d cafe:babe` 应显示 bcdDevice 1.11，
alt 8 的两个格式类型字段均为 Type III，bmFormats 为 0x00000381。

## CM4 实测步骤

1. 用 `aplay -l` 查找 Pico 的 ALSA 声卡号，下文以 `hw:2,0` 为例，需要替换。
   查看 `/proc/asound/card2/stream0`，应有 alt 8、S16_LE、2 channels、192000 Hz；
   原 24/32 位设置不应出现 192000。若没有 alt 8，先核对固件版本和重新枚举。
2. 检查选定音轨确实为 E-AC-3 / 48000 Hz：

```sh
ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,sample_rate,channels -of default=nw=1 input.mp4
ffmpeg -i input.mp4 -map 0:a:0 -c:a copy -f spdif eac3.spdif
```

`-map 0:a:0` 是第一个音轨，按实际文件调整。必须使用 `-c:a copy`，不要改成编码器
`-c:a eac3`，否则会重新编码且不能据此保留 JOC。不要直接将裸 `.eac3` 文件当 PCM 播放。

3. 用硬件设备按原字节发送封装后的载波：

```sh
aplay -D hw:2,0 -t raw -f S16_LE -c 2 -r 192000 eac3.spdif
```

不要用 `default`、`plughw`、混音、重采样或软件音量。此处 `-r 192000` 是载波速率，
不是把源音轨重采样。该实验仅覆盖 48 kHz E-AC-3，不覆盖 44.1/32 kHz 源、TrueHD/MAT、DTS-HD。

4. 同时用 `python tools/audio_monitor.py --seconds 65` 检查当前采样率为 192000，
   暖机后欠载、丢帧、坏包、PIO stall 和队列丢包计数不持续增长。
5. 接收端必须明确支持该 SPDIF 输入上的 E-AC-3 和 192 kHz non-PCM 载波。
   仅支持 AC-3/DTS 的 SPDIF 解码器不会因为 USB 能承载而支持 DD+；很多接收设备的
   DD+/Atmos 能力只对 HDMI/ARC 输入开放。以接收设备的规格和实际显示为准。
6. 停止后播放原 48/96 kHz PCM，确认输出恢复；验证媒体键与 LED 仍正常。

## 已验证与限制

`python tests/run_tests.py` 增加了默认/实验描述符、独立时钟范围/只读性、
普通时钟拒绝 192 kHz、192 kHz channel status、完整两次 burst 跨包逐字节保持、
音量/静音绕过、超长包拒绝及返回 PCM 的测试。burst 测试使用合成载荷，验证传输保真，
同时已使用真实 E-AC-3 杜比 Atmos 音轨和兼容条形音响完成播放验证。

完整 UAC3 迁移还需要 High Capability/Cluster/Power Domain 等描述符和相应控制请求；
不能把 D21 写入现有 UAC2 bmFormats 来代替这些工作。
