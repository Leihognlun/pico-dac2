# TF 卡 E-AC-3 → SPDIF 透传

`arc-tx-eac3` 分支新增 [CEC TV / ARC TX 测试版本](CEC_ARC.md)：GPIO8 输出音频、
GPIO18 接 CEC，只有 ARC 握手成功后才播放。下方 GPIO22 / BOTH 的可用版本是
**原无 CEC 基线**，归档保持不变；在新分支重建旧接线需显式设置
`PICODAC_CEC=OFF` 和 `PICODAC_SPDIF_PIN=22`。

`PICODAC_INPUT=SD_EAC3` 是独立的本地播放模式：上电挂载 TF 卡，读取指定的
**裸 E-AC-3 文件**，在 Pico 上封装 IEC 61937，经现有 SPDIF PIO/DMA 后端输出。
默认 USB 声卡模式仍为 `PICODAC_INPUT=USB`，两种输入通过构建选项选择。

## 当前可用版本（2026-09-16 用户确认）

采用无串口日志、保留诊断逻辑的版本作为当前 TF 播放可用基线。
用户确认该版未出现音频卡顿；此前同一播放链路已能出声，Soundbar 显示 Atmos。
本次确认未记录播放总时长，不扩展为所有文件、TF 卡或接收器的稳定性保证。
最初普通版卡顿的根因仍未确定，暂不删除诊断计时、原子计数及空闲处理逻辑。

已原样保存实测固件（未重新编译）：

- `build/releases/mdac_sd_eac3_usable_20260916.uf2`，219648 字节。
- SHA-256：`eaa87e934044938722e2befa101810496f82442c55a528e91e9ca79fea488c9e`。
- 来源：`build/sd-eac3-diag-silent/mdac_adc2.uf2`。
- 配置：Release、`PICODAC_INPUT=SD_EAC3`、`PICODAC_OUTPUT=BOTH`、
  `PICODAC_SD_DIAGNOSTICS=ON`、`PICODAC_SD_UART_LOG=OFF`。
- 文件 `0:/TRACK.EC3`，SPI0 GPIO2/3/4/5，SPI 12 MHz，SPDIF GPIO22，系统时钟 120 MHz。

`build` 目录不受 Git 跟踪，归档固件需另外备份。后续重编译可用基线时应保留上述
两个诊断选项，不能以 `PICODAC_SD_DIAGNOSTICS=OFF` 的普通版替代。

## 接线

按本工程实际接线使用硬件 SPI0：

| TF 模块信号 | Pico GPIO | 标准 Pico 物理引脚 |
| --- | --- | --- |
| SCLK / CLK | GPIO 2 | 4 |
| TX / MOSI / DI | GPIO 3 | 5 |
| RX / MISO / DO | GPIO 4 | 6 |
| CS | GPIO 5 | 7 |
| GND | GND | 8（或其他 GND） |
| 3.3 V 电源 | 3V3(OUT) | 36 |
| SPDIF 输出 | GPIO 22 | 29 |

使用兼容 3.3 V 逻辑的 TF 模块，供电按模块规格确认；裸卡使用 3.3 V。
信号线尽量短，共地。SPDIF 的光纤/同轴驱动要求见 [SPDIF.md](SPDIF.md)。
SPI 初始化频率为 100 kHz，读取目标频率默认 12 MHz，可在构建时降低。

## 文件与播放行为

1. 使用 FAT16、FAT32 或 exFAT 文件系统的 TF 卡。
2. 将 **48 kHz 源采样率**、big-endian 同步字 `0B 77` 的裸 E-AC-3 文件
   放到卡根目录，命名为 `TRACK.EC3`。`.eac3` 只是另一种扩展名，
   改名不做转码；也可用 `PICODAC_SD_FILE` 指定其他路径。
3. 断电插卡，烧录 TF 版固件后上电，自动播放一次。
4. 播放结束后维持 Non-PCM 零载波，避免截断最后的 DMA 块；重启可重播。

该模式不启动 USB 声卡或 HID，不实现 USB/TF 混音、按键选曲、热插拔重试或播放列表。
USB 接线可仅用于供电。需要原有 USB 声卡功能时烧录 USB 版固件。
默认 `BOTH` 输出下，I2S 保持与 SPDIF 同步的时钟，但数据始终为零。

支持单个独立子流（substream ID 0）、其 ID 0 dependent frames，及
E-AC-3 converted stream；帧的音频块数支持 1、2、3、6，按六块组成突发。
压缩载荷和元数据按位保留，不做解码、音量、重采样或 Atmos 元数据重写。
不支持 MKV/MP4/WAV 容器、已封装的 `.spdif`、AC-3、TrueHD、DTS、非 48 kHz 源
及其他 substream ID。文件须从完整帧集边界开始。

封装器检查同步字、帧长度、类型、块数和容量，不做完整 E-AC-3 解码或 CRC 校验。
损坏头部、截断帧、不完整的六块帧集、超出容量或读卡错误会终止文件读取；
已排队的完整突发播放完后转为零载波，不发送未完成的突发。

## 构建与固件

建议使用独立目录，保留 `build/mdac_adc2.uf2` 的 USB 固件：

```powershell
& "$env:USERPROFILE/.pico-sdk/cmake/v4.3.4/bin/cmake.exe" -S . -B build/sd-eac3 -G Ninja `
  "-DCMAKE_MAKE_PROGRAM=$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe" `
  -DCMAKE_BUILD_TYPE=Release -DPICODAC_INPUT=SD_EAC3 -DPICODAC_OUTPUT=BOTH `
  -DPICODAC_CEC=OFF -DPICODAC_SPDIF_PIN=22
& "$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe" -C build/sd-eac3
```

生成 **`build/sd-eac3/mdac_adc2.uf2`**，按 BOOTSEL 流程烧录。
上面的命令为普通版构建；重建当前可用基线请改用 `build/sd-eac3-diag-silent` 目录，
并增加 `-DPICODAC_SD_DIAGNOSTICS=ON -DPICODAC_SD_UART_LOG=OFF`。
仅需 SPDIF 时把 `PICODAC_OUTPUT` 改成 `SPDIF`；`I2S` 单输出不接受 TF 透传模式。

首次配置通过 CMake FetchContent 下载
[pico_fatfs](https://github.com/elehobica/pico_fatfs/tree/417fca7fa519a915a7a697e3edf7b9a183243eaa)，
固定提交 `417fca7fa519a915a7a697e3edf7b9a183243eaa`，需要 Git 和网络。
也可通过 `-DFETCHCONTENT_SOURCE_DIR_PICO_FATFS=<该提交的本地绝对路径>` 使用已有源码。
USB 模式不会获取或链接此依赖。第三方许可见 [LICENSE.pico-fatfs](LICENSE.pico-fatfs)。

可选参数：

| CMake 参数 | 默认值 | 用途 |
| --- | --- | --- |
| `PICODAC_SD_FILE` | `0:/TRACK.EC3` | 指定文件；路径限 ASCII 字母、数字、下划线、点、斜杠和连字符 |
| `PICODAC_SD_SCK_PIN` | `2` | SPI0 SCLK |
| `PICODAC_SD_MOSI_PIN` | `3` | SPI0 TX |
| `PICODAC_SD_MISO_PIN` | `4` | SPI0 RX |
| `PICODAC_SD_CS_PIN` | `5` | CS |
| `PICODAC_SD_SPI_HZ` | `12000000` | SPI 读取频率（100000–25000000 Hz） |

配置时检查硬件 SPI0 引脚合法性和音频/UART/控制引脚冲突，禁止自动回退到 PIO SPI。

恢复/编译 USB 模式：

```powershell
& "$env:USERPROFILE/.pico-sdk/cmake/v4.3.4/bin/cmake.exe" -S . -B build -DPICODAC_INPUT=USB
& "$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe" -C build
```

## 数据通路与时钟

- Core 1：独占 FatFs 和 SPI，8 KB 读缓存，解析帧并生成完整 IEC 61937 突发。
- 四槽单生产者/单消费者队列：每槽 24576 字节，使用 acquire/release 原子操作交接；
  最后一段载波数据被复制后才释放槽位。启动先预缓冲，短文件在 EOF 后也可启动。
- Core 0：每次向现有 SPDIF 后端交付 192 帧，32 个 SPDIF 块组成一个突发。
  队列空时按完整突发周期发送零载波，恢复时从下一完整突发边界继续。

E-AC-3 的 Pa/Pb 为 `0xF872/0x4E1F`，Pc 为 `0x0015`，Pd 是载荷**字节数**。
每突发包含 1536 个源样本对应的压缩数据，补齐至 6144 个立体声载波帧，
以 192 kHz 输出，名义周期 32 ms。四槽可保存 128 ms 载波数据，
但实际抗读卡延迟能力取决于当时剩余的完整突发，不能保证任意慢卡都无欠载。

TF 模式系统时钟为 120 MHz：`1440 MHz / 6 / 2`。
PIO 的 1/256 分频单位值为 `120000000 / 192000 = 625`，消除平均分频取整误差；
晶振误差、分数分频抖动仍然存在。USB 模式保留原来的 132 MHz 时钟。

## 日志与验证

### UART0 周期诊断固件

用于定位“约播放 2 秒、卡顿 2 秒”的诊断版位于
**`build/sd-eac3-diag/mdac_adc2.uf2`**，仍读取根目录 `TRACK.EC3`，TF 接线不变。
这是诊断版，不包含针对卡顿原因的修复。

- Pico **GPIO0 / 物理引脚1（UART0 TX）→ USB-TTL 转接器 RX**。
- Pico GND → 转接器 GND。只收日志无需连接 GPIO1（UART0 RX）。
- 使用 **3.3 V TTL，115200 波特率，8N1，无流控**；不是 RS-232 电平。
- 先打开串口记录，再插好 TF 卡并复位，采集从启动开始至少 30 秒的完整日志，
  标注听到卡顿的大致时间。日志不通过 Pico USB 端口输出。

配置方法与普通 TF 版相同，改用 `-B build/sd-eac3-diag`，
并增加 `-DPICODAC_SD_DIAGNOSTICS=ON`；使用 SDK Ninja 编译该目录。
诊断选项默认关闭，原有普通 TF/USB 固件不启用周期日志。

启动打印 `SDDIAG` 标识，之后每秒一行 `SD ...`：

| 字段 | 含义 |
| --- | --- |
| `t_ms` | 诊断启动后的毫秒数（约 71 分钟回绕） |
| `st` | 播放状态：0 预缓冲、1 播放、2 EOF，负数见 `eac3_burst.h` |
| `ph` | 读线程阶段：0 初始化、1 挂载、2 打开、3 读卡、4 封装、5 等待空队列槽、6 读取结束 |
| `q` / `qmin` | 当前已发布但未释放的突发槽数／本报告间隔最低值，范围 0–4，包含正在消费的槽 |
| `prod` / `play` | 累计生产／完整提交给 SPDIF 的突发数；正常持续播放每秒约 31–32 个 |
| `under` | 文件突发队列欠载次数 |
| `dma0` | SPDIF DMA 未取得下一编码块而发零的累计次数，启动少量增长正常 |
| `txstall` | SPDIF / I2S 的 PIO 停顿累计计数 |
| `reads` / `bytes` | FatFs 读取调用数／成功返回的原始文件字节数，非载波字节数 |
| `rdmax_us` | 启动以来最慢一次 FatFs 读取耗时（微秒） |
| `rdage_us` | 当前仍在进行的 FatFs 读取已等待多久（微秒），非读卡阶段为 0 |
| `fr` | 最近一次 FatFs 读取结果，0 为成功 |
| `gapmax_us` | 启动以来两次 SPDIF 块提交开始之间的最大间隔（微秒），常规周期约 1000 |
| `encmax_us` | 启动以来拷贝、编码并提交一块的最大耗时（微秒） |

`(+N)` 是相对于上一行的增量，其余最大值为启动后的累计最大值。
跨核心快照非严格同时采样，应用于定位趋势而非逐周期时序测量。
首行 `qmin=0` 可来自预缓冲阶段，不据此判定持续播放异常。

若 `under` 增长、`qmin=0` 且 `rdage_us`/`rdmax_us` 很大，优先排查读卡供数。
若队列有数据但 `dma0`/`txstall` 增长，结合 `gapmax_us`、`encmax_us` 排查输出供数。
若计数稳定而仍有卡顿，需要进一步检查载波/突发时序及接收器重新锁定行为。

播放期间周期日志由 Core 0 格式化到固定缓冲区，每次空闲检查只在 UART FIFO 有空间时
发送最多 8 字节，不等待整行发完；Core 1 只发布原子诊断数据。
读取结束的普通 `printf` 在诊断版关闭，以免与周期 UART 输出交错。
诊断仍有执行开销，应对照普通版本的听感记录是否改变。

### 普通启动日志与测试

静默对照固件：`build/sd-eac3-diag-silent/mdac_adc2.uf2`。
构建参数为 `PICODAC_SD_DIAGNOSTICS=ON`、`PICODAC_SD_UART_LOG=OFF`：
保留诊断计时、原子计数、每秒格式化和空闲处理，丢弃日志而不发送 UART，
同时关闭 stdio UART，因此启动/错误日志也不会输出。
这不是把诊断代码整体关闭；用于判断串口输出本身是否影响卡顿。
去掉 UART 发送仍会改变执行时序，不能视为与有日志版完全相同的时序。

UART0 TX=GPIO0，115200 波特率，3.3 V 串口、共地。
启动日志列出文件与引脚，挂载/打开失败报告 FatFs 错误码；
读线程结束时报告 EOF 或解析/读取错误及累计入队突发数。
读线程结束不表示队列中的音频已经输出完毕。

调试器可读取：

| 变量 | 含义 |
| --- | --- |
| `sd_eac3_status` | 0 等待，1 播放，2 EOF 后零载波，负数为错误（见 `eac3_burst.h`） |
| `sd_eac3_bursts_played` | 已完整提交给 SPDIF 后端的文件突发数（不等于已到达功放） |
| `sd_eac3_underruns` | 读线程尚未结束但突发队列为空的周期数 |
| `spdif_silence_block_count` | DMA 未及时得到编码块的次数，启动也可能增长 |
| `spdif_tx_stall_count` / `i2s_tx_stall_count` | PIO 停顿计数 |

运行 `python tests/run_tests.py`：新增测试检查 1/2/3/6 块分组、dependent payload
保留、Pd 字节数、字节序、零填充、SPDIF 编码往返、短读、错误头部、截断和容量上限；
线程化播放测试替换 FatFs 和硬件，检查完整数据顺序、单突发短文件、挂载/打开失败、
中途读错、读卡阻塞后的欠载与恢复以及 EOF 排空。

上述自动化检查是软件测试。TF 无串口日志版已由用户确认无卡顿，实测范围见本文开头；
现有 USB 输入链路的 E-AC-3/Atmos 实测结果不能代替其他 TF 播放条件的验证。
首次上板请使用此前已在接收器上播放成功的 48 kHz 裸 E-AC-3 文件，检查格式显示、
连续播放及计数，并复测短文件、缺文件和读卡失败。是否识别 Atmos 取决于原始音轨和接收器。

## 参考

- [elehobica/pico_spdif_recorder](https://github.com/elehobica/pico_spdif_recorder)：参考其
  Pico 上 SPI/FatFs 接入方式，直接使用其上游 `pico_fatfs` 库；未引入录音、联网或 I2S 接收逻辑。
- [WeebLabs/DSPi](https://github.com/WeebLabs/DSPi)：参考双核音频处理与 PIO/DMA 输出架构；
  该项目主要处理 PCM，本实现不复制其 GPL DSP 代码，不让压缩载荷经过 DSP。
- [FFmpeg IEC 61937 muxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/spdifenc.c)：
  核对 E-AC-3 突发类型、长度单位和重复周期；本工程的流式读取/封装代码独立实现。
