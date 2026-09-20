# 程序运行逻辑与外设配置

日期：2026-09-18。范围：`arc-tx-eac3` 分支当前 CEC、DDC、TF 与 USB 实现。
本文描述软件逻辑，不代表所有功能已通过 Soundbar 实机验证。
流程图采用 Mermaid；查看时需要支持 Mermaid 的 Markdown 预览器。

## 1. 总体结构

程序分为三条相对独立的链路：

- 音频：TF 或 USB → 音频缓冲 → SPDIF → 外部 ARC 电路。
- 控制：CEC → ARC 开关、Soundbar 音量。
- 信息：DDC → Soundbar 读取内置 EDID。

**DDC 被读取不会直接开启音频；CEC 握手成功也不意味着已有音频数据。**

```mermaid
flowchart TB
    subgraph INPUT[音频输入：编译时二选一]
        TF[TF 卡 / SPI0 / 裸 EAC3]
        USB[USB 主机 / PCM 或 IEC 61937]
    end
    TF --> READER[Core1：FatFs 读取、解析和封装]
    READER --> BQ[4 槽完整 burst 队列]
    BQ --> SD[Core0：TF 播放循环]
    USB --> USBIRQ[USB 中断：保存数据包快照]
    USBIRQ --> USBTASK[Core0：USB 事件处理]
    USBTASK --> RB[音频环形缓冲]
    RB --> AUDIO[Core0：音频状态机]
    SD --> ENC[SPDIF 编码：每块 192 个双声道帧]
    AUDIO --> ENC
    ENC --> DMA[输出双缓冲队列 → DMA]
    DMA --> PIO[PIO0 → GPIO8]
    PIO --> ARC[外部 SPDIF → ARC 电路]
    ARC --> SB[Soundbar]
    KEYS[GP10 开关 / GP26 加 / GP27 减] --> CEC[Core0：CEC TV 状态机]
    CEC <--> WIRE[50 us 定时回调：CEC 位级收发]
    WIRE <-->|GPIO18| SB
    CEC -.->|允许或关闭载波| DMA
    SB -->|DDC 主机读取| I2C[I2C1 从设备 0x50 / GP6、GP7]
    EDID[Flash 中只读 EDID：256 字节] --> I2C
    I2C -->|返回 EDID| SB
```

没有 FreeRTOS：采用主循环、中断与 PIO/DMA；只有 TF 模式启动 Core1。
TF 与 USB 是不同构建，不是运行时自动切换。

## 2. 外设配置

下表编号均为 GPIO 编号，而非 Pico 物理引脚编号。

| 模块 | 当前配置 | 执行位置或用途 |
| --- | --- | --- |
| 系统时钟 | TF 120 MHz；USB 132 MHz | `main()` 启动时设置 |
| TF SPI | SPI0；SCLK=GP2、MOSI=GP3、MISO=GP4、CS=GP5 | Core1 读卡 |
| TF SPI 速率 | 初始化配置 100 kHz；正常传输配置 12 MHz | TF 驱动使用 |
| DDC | I2C1 从设备；SDA=GP6、SCL=GP7；7 位地址 0x50 | I2C 中断处理 |
| DDC 时钟 | 初始化参数 100 kHz；总线实际时钟由主机提供 | 支持硬件时钟拉伸 |
| DDC 上拉 | 内部上拉关闭 | 外部上拉、电平转换 |
| SPDIF | PIO0，GPIO8，动态申请状态机 | 外部 ARC 转换电路输入 |
| SPDIF DMA | 动态申请通道；32 位传输；PIO TX DREQ | 源地址递增，目标固定为 TX FIFO |
| DMA 中断 | DMA_IRQ_1，最高中断优先级 | 完成一块后接续下一块 |
| CEC | GP18，开漏式输出，无内部上拉 | 拉低或切为输入释放总线 |
| CEC 定时 | 每 50 μs 回调 | 收发时序、采样、ACK |
| ARC 开关键 | GP10，输入上拉，低有效 | 20 ms 消抖 |
| 音量加 / 减 | GP26 / GP27，输入上拉，低有效 | 长按约每 300 ms 重复 |
| 板载 LED | PIO1；标准 Pico 为 GP25 | USB 音频状态指示 |
| USB 控制 LED | GP9、GP12；GP11、GP14 配为低电平输出 | HID 控制 |
| 原第三路 LED | GP26/GP28 不作为 LED 初始化 | 避免音量加按键冲突 |
| UART | 当前 TF 版关闭日志输出 | 保留 SD 诊断计时、计数 |
| I2S | 两个 ARC 构建均禁用 | GP18 已供 CEC 使用 |

PIO 状态机及 DMA 通道编号由运行时申请，不保证为 SM0 或 DMA0。
标准 Pico 上 GP6/7/8/18/26 分别对应物理脚 9/10/11/24/31。

## 3. 上电初始化

入口：[main.c](main.c)。

```mermaid
flowchart TD
    BOOT[上电或复位] --> CLOCK[系统时钟：TF 120 MHz / USB 132 MHz]
    CLOCK --> STDIO[stdio_init_all]
    STDIO --> LED[blink_init：分配 PIO1 状态机]
    LED --> DDC[ddc_edid_init：I2C1 地址 0x50]
    DDC --> CEC[cec_arc_init]
    CEC --> GATE[关闭 SPDIF 链路许可 / 配置 GP18]
    GATE --> KEYS[配置三个按键 / 启动 50 us 定时回调]
    KEYS --> MODE{编译输入模式}
    MODE -->|SD_EAC3| SDINIT[初始化诊断 / 启动 Core1]
    SDINIT --> PRE[Core0 等待预缓冲 / 继续处理 CEC]
    PRE --> SDLOOP[TF 播放循环]
    MODE -->|USB| UINIT[初始化音频缓冲、SPDIF、USB Audio、HID]
    UINIT --> ULOOP[usb_device_task → audio_device_task → cec_arc_task]
    ULOOP --> ULOOP
```

DDC 早于音频输入初始化，即使 TF 挂载失败仍可读取 EDID。
CEC 初始化关闭输出许可，避免未完成 ARC 握手就输出载波。

## 4. TF 双核播放流程

实现：[sd_eac3_player.c](sd_eac3_player.c)。只有 Core1 访问 FatFs。

```mermaid
flowchart TD
    subgraph CORE1[Core1：生产完整 burst]
        SPI[配置 SPI0] --> MOUNT[挂载 FatFs]
        MOUNT --> OPEN[打开 0:/TRACK.EC3]
        OPEN --> FULL{4 槽队列已满？}
        FULL -->|是| WAIT[sleep_us 100 等待]
        WAIT --> FULL
        FULL -->|否| READ[8192 字节缓存读取文件]
        READ --> PACK[解析 EAC3 / 生成 IEC 61937 burst]
        PACK --> RESULT{解析结果}
        RESULT -->|完整 burst| PUB[发布 produced 计数]
        PUB --> FULL
        RESULT -->|EOF 或错误| END[关闭文件、卸载 / 发布 producer_result]
    end
    PUB --> QUEUE[共享队列：4 × 24576 字节]
    subgraph CORE0[Core0：消费与输出]
        PRE[等待 4 槽或生产者结束] --> ANY{存在完整 burst？}
        ANY -->|否| IDLE[记录状态 / 持续处理 CEC 与诊断]
        ANY -->|是| INIT[初始化 SPDIF：192 kHz、16 bit、Non-PCM / 请求启动]
        INIT --> GATE{CEC 允许 ARC？}
        GATE -->|否| HOLD[不消费队列 / burst 偏移归零 / 处理 CEC 与诊断]
        HOLD --> GATE
        GATE -->|是| READY{有可写 SPDIF 块？}
        READY -->|否| TASK[处理 CEC 与诊断]
        TASK --> GATE
        READY -->|是| COPY[复制 192 帧 / 缺数据则填零]
        COPY --> SUBMIT[编码并发布输出块]
        SUBMIT --> DONE{当前 burst 全部提交？}
        DONE -->|否| GATE
        DONE -->|是| RELEASE[推进 consumed / 释放槽位]
        RELEASE --> GATE
    end
    QUEUE --> COPY
    RELEASE -.->|原子计数同步| FULL
```

| 缓冲层级 | 大小 | 对应时间 |
| --- | --- | --- |
| 文件读缓存 | 8192 字节 | 取决于 EAC3 码率 |
| 一个 IEC 61937 burst | 24576 字节 | 32 ms |
| 四槽 burst 队列 | 98304 字节，即 96 KiB | 最多约 128 ms |
| 一个 SPDIF 输出块 | 192 个双声道帧 | 192 kHz 下为 1 ms |
| 一个 burst | 32 个 SPDIF 输出块 | 32 ms |

Core1 只发布封装完成的数据；Core0 提交完一个 burst 的最后一部分才释放槽位。
这避免两个核心同时改写同一槽位。

### EAC3 封装

实现：[eac3_burst.c](eac3_burst.c)。

```mermaid
flowchart LR
    RAW[裸 EAC3] --> CHECK[检查同步头、长度、采样率、子流及块数]
    CHECK --> GROUP[累计 6 个音频块 / 保留关联 dependent 帧]
    GROUP --> IEC[添加 Pa、Pb、Pc、Pd]
    IEC --> PAD[复制压缩 payload / 补零]
    PAD --> BURST[24576 字节完整 burst]
```

当前接受 48 kHz 裸 EAC3、独立子流 ID 0 及其关联帧。
这里只封装 IEC 61937，不进行 Dolby 解码；EAC3 的 Pd 使用 payload 字节数。

## 5. SPDIF、DMA 与链路许可

实现：[spdif.c](spdif.c)、[spdif_encode.c](spdif_encode.c)。

```mermaid
flowchart TD
    APP[应用调用 spdif_start] --> REQ[start_requested = true]
    REQ --> COND{已初始化且 link_enabled？}
    COND -->|否| WAIT[保留请求 / 暂不启动]
    COND -->|是| START[启动 DMA / 预填 FIFO / 启用 PIO]
    BUF[Core0 编码完成的块] --> Q[输出块所有权队列]
    START --> IRQ[DMA 完成中断]
    IRQ --> NEXT{下一块已发布？}
    Q --> NEXT
    NEXT -->|是| DATA[DMA 读取音频块]
    NEXT -->|否| ZERO[DMA 读取零数据块 / 累计 silence]
    DATA --> PIO[PIO TX FIFO → GPIO8]
    ZERO --> PIO
    PIO --> IRQ
    OFF[CEC 关闭 ARC] --> STOP[停 DMA 和 PIO / 清 FIFO / GP8 拉低 / 重置队列]
    STOP --> KEEP[保留应用启动意图]
    ON[CEC 握手成功] --> ENABLE[link_enabled = true]
    ENABLE --> REQUEST{应用已请求启动？}
    REQUEST -->|是| START
```

实际载波启动必须同时满足：SPDIF 已初始化、应用请求播放、TV 注册成功、无地址冲突、
ARC 状态 ON、用户仍要求开启。

编码后每块为 `768 × uint32_t`，即 3072 字节；DMA 只搬运编码数据，不解析 EAC3。
ARC 关闭会停止物理载波；ARC 开启但缺数据时继续输出零数据载波，压缩模式保留 Non-PCM 状态。

## 6. CEC 分层、时序与握手

实现：[cec_arc.c](cec_arc.c)、[cec_tv.c](cec_tv.c)、[cec_wire.c](cec_wire.c)。

```mermaid
flowchart TB
    BTN[按键变化 / 20 ms 消抖] --> ARC[cec_arc_task：主循环协调层]
    RX[接收帧邮箱] --> ARC
    RESULT[发送结果] --> ARC
    ARC --> TV[cec_tv：地址注册、ARC、音量、查询响应]
    TV --> TXQ[8 帧发送队列]
    TXQ --> WIRE[cec_wire：每帧最多 16 字节]
    TIMER[50 us 定时回调] --> WIRE
    WIRE <-->|拉低、释放、采样| PIN[GP18]
    WIRE --> RX
    WIRE --> RESULT
    ARC --> GATE[更新 SPDIF 链路许可]
```

定时回调只负责位级收发，不运行整套 ARC 业务，也不打印日志。

| 项目 | 当前代码值 |
| --- | --- |
| 定时采样 | 50 μs |
| 起始位低电平 / 总周期 | 3700 / 4500 μs |
| 数据 1 / 0 低电平 | 600 / 1500 μs |
| 数据位总周期 | 2400 μs |
| ACK / 竞争采样点 | 位开始后 1050 μs |
| 发送前空闲等待 | 16800 μs |
| 发送等待超时 | 2 秒 |
| 发送调度严重迟到 | 超过期限 200 μs，释放总线并失败 |
| 普通帧发送尝试 | 最多 3 次 |

```mermaid
sequenceDiagram
    participant K as 按键或开机
    participant TV as Pico TV 地址0
    participant SB as Soundbar 地址5
    participant OUT as SPDIF GP8
    K->>TV: 开机默认请求开启
    TV->>OUT: 关闭载波许可
    Note over TV: 约1秒后轮询地址0
    TV->>TV: 发送轮询头00
    alt 地址0已有设备ACK
        Note over TV: 标记冲突，保持输出关闭
    else 地址0无ACK
        Note over TV: 注册为TV
        TV->>SB: 广播物理地址0.0.0.0
        TV->>SB: 05 70 00 00 请求系统音频
        TV->>SB: 05 C3 请求开启ARC
        SB->>TV: 50 C0 Initiate ARC
        TV->>SB: 05 C1 Report ARC Initiated
        SB-->>TV: C1得到ACK
        TV->>OUT: 允许载波，已有播放请求则启动
    end
    K->>TV: GP10关闭
    TV->>OUT: 停止载波
    TV->>SB: 05 C4 请求终止ARC
    TV->>SB: 05 70 关闭系统音频
    SB->>TV: 50 C5 Terminate ARC
    TV->>SB: 05 C2 Report ARC Terminated
```

等待开启、报告完成等阶段超时为 4 秒；仍有开启意愿时，之后等待 5 秒重试。
Soundbar 主动 C5 同样关闭输出。明确拒绝、Standby 或系统音频关闭后，不强行持续开启。
GP26/27 发送音量按下消息 `44 41/42`，松开发送 `45`，不修改压缩音频。
没有 HPD 拔线检测，拔线不保证自动改变 ARC 状态。

## 7. DDC EDID 读取

实现：[ddc_edid.c](ddc_edid.c)、[ddc_edid_data.c](ddc_edid_data.c)。

```mermaid
sequenceDiagram
    participant SB as Soundbar主机
    participant HW as I2C1
    participant ISR as DDC回调
    participant ROM as EDID 256字节
    SB->>HW: START + 0x50写 + 偏移00
    HW->>ISR: RECEIVE
    ISR->>ISR: offset=00，记录偏移已写
    SB->>HW: repeated START
    HW->>ISR: FINISH
    ISR->>ISR: 清写事务标记，保留offset
    SB->>HW: 0x50读
    loop 每请求一个字节
        HW->>ISR: REQUEST
        ISR->>ROM: 读取EDID[offset]
        ROM-->>ISR: 字节
        ISR->>HW: 写TX FIFO，offset递增
        HW-->>SB: 返回字节
    end
    SB->>HW: NACK + STOP
    HW->>ISR: FINISH
    ISR->>ISR: 保留下一次读取位置
```

- 0x00～0x7F 是基本块，0x80～0xFF 是扩展块，读完回绕至 0x00。
- 写事务首字节设置偏移，后续字节丢弃，EDID 不可修改。
- 不响应 0x30 段指针。
- 不依赖 TF，不等待 CEC；没有“读完 EDID 才启动 CEC”的联动条件。
- 数据原样来自用户提供的 `ep-edid-retek-2000.bin`。

## 8. USB 音频模式

实现：[usb.c](usb.c)、[usb_audio.c](usb_audio.c)、[audio_device.c](audio_device.c)。

```mermaid
flowchart TD
    HOST[USB主机发送音频] --> IRQ[USB中断：复制DPRAM快照 / 重新准备接收]
    IRQ --> EVENTS[USB事件队列]
    EVENTS --> TASK[usb_device_task / 丢弃过期流数据包]
    TASK --> FORMAT[检查长度与帧对齐 / 转换int32样本容器]
    FORMAT --> RING[约16 ms环形缓冲]
    RING --> STATE[audio_device_task]
    STATE --> BUFFER[BUFFERING：水位达到50%请求启动]
    BUFFER --> PLAY[PLAYING]
    PLAY --> TYPE{PCM或Non-PCM？}
    TYPE -->|PCM| GAIN[主音量、左右音量、静音]
    TYPE -->|Non-PCM| PASS[原样复制IEC 61937字]
    GAIN --> SPDIF[SPDIF输出 / 受CEC许可控制]
    PASS --> SPDIF
    PLAY -->|水位不高于16%或不足一块| STALL[STALLED：输出零数据]
    STALL -->|水位恢复40%| PLAY
    RING -.->|水位| FB[USB feedback 微调主机发送速率]
    FB -.-> HOST
```

TF 模式由 Pico 封装裸 EAC3；USB 压缩模式要求主机已完成 IEC 61937 封装。
TF 在 ARC 关闭时暂停消费；USB 不会自动暂停电脑播放器，仍可能接收数据并溢出丢帧，
恢复时接收器需重新同步后续 burst。

## 9. 异常与边界行为

| 情况 | 当前行为 |
| --- | --- |
| TF 挂载或打开失败 | 不启动音频，继续 CEC 与 DDC |
| 文件结束 | 排空数据；ARC 开启时继续 Non-PCM 零数据载波 |
| TF 暂时供数不足 | 补零并累计 underrun |
| ARC 中途关闭 | 停 DMA/PIO，暂停消费 TF 队列 |
| ARC 重新开启 | TF 从当前完整 burst 头部重新提交 |
| CEC 地址0占用 | 标记冲突，不开启 ARC |
| CEC 发送失败 | 有限重试，结果交给 TV 状态机 |
| DDC 数据写入 | 丢弃，保持只读 |
| GP26 按下 | 向 Soundbar 发音量加，不驱动原 LED |
| Soundbar 不读 EDID | 不直接阻止 CEC 状态机运行 |

## 10. 调试观察点与硬件边界

可通过调试器观察以下变量：

- CEC：`cec_rx_frames`、`cec_tx_frames`、`cec_tx_errors`、`cec_rx_errors`、`cec_address_conflict`。
- ARC：`cec_arc_state`，0=关闭、1=等待C0、2=等待C1成功、3=开启、4=正在关闭。
- DDC：`ddc_read_bytes`、`ddc_offset_writes`、`ddc_ignored_writes`。
- TF：`sd_eac3_status`、`sd_eac3_bursts_played`、`sd_eac3_underruns`。
- SPDIF：`spdif_tx_stall_count`、`spdif_silence_block_count`。

HPD、HDMI +5V、DDC 双向电平转换及 SPDIF→ARC 电气转换由外部硬件负责。
GPIO 不可直接接入 5V 上拉总线。本程序不是完整 HDMI TV 或 eARC 实现。

相关接线、构建与限制详见 [CEC_ARC.md](CEC_ARC.md)、[SD_EAC3.md](SD_EAC3.md)、[SPDIF.md](SPDIF.md)。
