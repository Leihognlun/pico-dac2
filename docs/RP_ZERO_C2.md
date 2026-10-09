# RP-ZERO-C1 / C2 board 配置

使用 `-DPICODAC_BOARD=c1` 或 `-DPICODAC_BOARD=c2` 选择板子，默认 `c1`。
GPIO 定义集中在 `boards/c1.h` 和 `boards/c2.h`，`src/board.h` 选择对应配置，
`boards/select.cmake` 读取同一份定义并校验引脚冲突。
`PICODAC_BOARD` 是应用接线配置，与 Pico SDK 的 `PICO_BOARD` 独立。
旧的单独 GPIO CMake 参数由所选 board 配置取代；修改接线应修改相应 board 文件。

## C2 引脚

以下均为 RP2040 GPIO 编号。UART0 TX/RX 为 0/1，TF SPI0 SCK/MOSI/MISO/CS
为 2/3/4/5，绿色代码/错误状态灯为 25。

| 功能 | ARC1 | ARC2 |
| --- | --- | --- |
| SPDIF TX | 23 | 18 |
| CEC | 24 | 22 |
| HDMI 5V 检测，高有效 | 29 | 19 |
| HPD，低有效 | 28（共用） | 28（共用） |
| DDC SDA | 26 / I2C1 | 20 / I2C0 |
| DDC SCL | 27 / I2C1 | 21 / I2C0 |
| 黄色 ARC 链路灯，高有效 | 17 | 16 |

GPIO21 对应 I2C0 SCL，截图中的 `I2C2_SCL` 按 I2C0 处理。
两路 DDC 均以地址 `0x50` 提供现有 EDID，分别保存读地址及事务状态。
两路 CEC 分别维护 TV/ARC 协商、发送队列和音量键状态。
DDC/EDID 初始化完成后，固件拉低并保持共用 HPD，表示 TV 侧已经准备好。外部硬件
分别用各连接器自身的 HDMI 5V 对该信号做门控，所以只有相应端口有 5V 时，连接器
上的 HPD 才实际有效。每路 5V 检测必须连续为高 100 ms 才确认插入，并只启动该路
CEC 注册与 ARC 协商；另一路不会被提前启动。每一路载波仍要求该路自己的插入确认
和 CEC ARC 协商成功。

| 按钮 | Key（低有效） | LED+（高有效） | 功能 |
| --- | --- | --- | --- |
| B1 | 7 | 6 | 选择 ARC1 播放；已选中时停止 |
| B2 | 9 | 8 | 选择 ARC2 播放；已选中时停止 |
| B3 | 11 | 10 | 下一首 |
| B4 | 13 | 12 | 音量加 |
| B5 | 15 | 14 | 音量减 |

C2 没有独立 LED- GPIO；C1 保留原有 LED- 低电平输出及四键行为。

## 播放行为

两路共用一个 TF 或 USB 音源、格式、采样率和播放进度，**同时最多一路选择实际音频载荷**。
上电不自动播放。SPDIF 初始化并启动后，已建立 ARC 链路的两路均持续发送载波。
按 B1 选择 ARC1 并点亮 B1 灯；按 B2 切换至 ARC2 并点亮 B2 灯、熄灭 B1 灯，反向同理。
再次按下当前已选中端口的键则停止，两路改发零载荷、V=1 的无效音频保活帧。
按任一播放键可重新选择输出。切换在 DMA 块边界更新载荷路由，旧端口继续发送保活帧；
实际载波仍需对应端口自身的 ARC 链路许可，断开链路的端口保持低电平。
B3/B4/B5 灯仅在选择播放时点亮；停止时这三个键不产生操作。
CEC/HPD/DDC 链路维护独立于音频播放，黄色灯结合 ARC 连接状态和本地播放选择显示。
HDMI Detect 从连接变为断开时，该端口会清空 CEC 消息队列并重置地址注册、
Soundbar 探测、ARC、System Audio、音量和冲突状态；重新插线后重新协商。

CEC 会分别监控两台 Soundbar 上报的音量。整台设备所有端口均停止播放达到
`BOARD_CEC_VOLUME_RESET_DELAY_MS` 后，固件才对所有已连接端口用音量加减键逐级调整到
`BOARD_CEC_DEFAULT_VOLUME ± BOARD_CEC_VOLUME_TOLERANCE` 范围内。默认值为
20、误差 2、等待 30000 ms，因此 18..22 均满足要求。参数位于 `boards/c2.h`。
音量查询只在对应端口处于 `ARC_ON` 时进行，ARC 未连接或正在协商时不发送 `05:71`。

TF 模式：上电可读卡预缓冲，但不消耗音频播放位置。B3 切换共享播放列表的下一首。
停止或所选端口未建立链路时，保留当前播放位置；切换端口时沿用当前文件与播放进度。
压缩音频恢复时等待完整 IEC 61937 burst 起点；不解码或重新编码 AC-3/E-AC-3。
连续播放两首后自动进入待机；10 秒内重新播放时从下一首开始，超过 10 秒则从列表第一首开始。

USB 模式：B1/B2 控制本地单路输出选择，主机音源继续前进。B3 发送 HID Next Track，
是否切歌取决于主机播放器。B4/B5 只向当前选中的 ARC 端口发送 CEC 音量键；
切换或停止时先释放旧端口已按住的音量键，按住的键不会自动转发为新端口的按下事件。
C2 按钮灯由固件管理，旧的主机 HID 灯写入
请求不再接受，诊断报告仍保留。C2 USB `bcdDevice=0x010b`。

C2 使用两组 PIO0 状态机和 DMA，启动时同步，音频编码缓冲只在两个 DMA 都完成后
复用。未选中端口持续输出保活帧，保持当前采样率和声道状态；无待播缓冲时 DMA
也会循环发送保活帧。显式停止/释放 SPDIF 或格式重建期间载波会中断。
黄色灯按各自端口独立显示：未连接时亮一秒、灭一秒；已连接但未选择播放时熄灭；
已连接且按播放选择键选中该端口时常亮。停止或切走时熄灭，未连接时选择仍保持闪烁。
绿色灯正常常亮，错误按错误码次数闪烁；TF/文件错误每轮亮灭各 4 次，轮间隔默认 2 秒。
配置与错误码见 [LED_STATUS.md](LED_STATUS.md)。
`PICODAC_CEC=OFF` 时省略链路许可检查，但仍需按键选择且只允许一路输出，黄色灯慢闪，音量键无 CEC 操作。

## 构建

在已配置 Pico SDK、ARM 工具链与 Ninja 的终端运行：

```powershell
cmake -S . -B build/dev-zero-c2-tf -G Ninja -DPICODAC_BOARD=c2 -DPICODAC_INPUT=SD_AUDIO -DPICODAC_SD_UART_LOG=ON
cmake --build build/dev-zero-c2-tf
cmake -S . -B build/dev-zero-c2-usb -G Ninja -DPICODAC_BOARD=c2 -DPICODAC_INPUT=USB
cmake --build build/dev-zero-c2-usb
```

输出分别为构建目录中的 `mdac_adc2.uf2`。C1 使用单独目录并指定
`-DPICODAC_BOARD=c1`，接线见 [RP_ZERO_C1.md](RP_ZERO_C1.md)。

`python tests/run_tests.py` 包含 C1 回归以及 C2 按键、HID、双 DDC、独立 CEC/HPD、
双 DMA 缓冲所有权、单路静音/断开、压缩音频恢复与格式重启测试。
这些主机测试和固件编译不能替代两台 Soundbar 同时连接时的电气、时序和听音实测。

### 5V Detect / CEC 启动日志

TF 构建可通过 UART0（TX GP0，115200 8N1）观察端口检测。配置
`PICODAC_SD_UART_LOG=ON` 后，启动和插拔会输出：

- 公共 HPD 已在 DDC 就绪后拉低；
- ARC1/ARC2 的 `5V_DET` GPIO 和启动时原始电平；
- 原始电平变高及 100 ms 去抖开始；
- 100 ms 确认后只启动对应端口 CEC；
- 去抖期间掉低或已连接端口拔线；
- TV 地址、Audio System 地址轮询以及 ARC 控制报文入队。

若 ARC1 未接 5V 却出现 `ARC1 ... HIGH` 或 `confirmed HIGH`，应优先检查 GP29 的外部
电平、上下拉、两个 Detect 信号是否接反或串扰。只有出现 `confirmed HIGH` 后，该端口
才允许开始 CEC 地址轮询。

`5V_DET` 由 CEC 的 50 us 定时回调连续采样，而不是只由主循环间歇检查。例如高电平
只保持 50 ms 后拉低，会输出 `debounce cancelled`，不会输出 `confirmed HIGH`，也不会
启动该端口的 CEC 地址轮询。
