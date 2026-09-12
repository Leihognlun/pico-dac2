# SPDIF 输出

工程默认构建 SPDIF 输出，原有 I2S 可通过 CMake 选项切换。两个输出模式不同时运行。

## 接线与构建

- 默认信号：**GPIO 22**（标准 Pico 的物理引脚 29）。
- 地：Pico GND 与发射模块 GND 共地。
- GPIO 输出为 3.3 V 数字逻辑信号，可接输入兼容 3.3 V 的光纤发射模块。
  发射模块供电按其规格连接。
- 同轴输出需要合适的 SPDIF 驱动、电平匹配及隔离电路；不要将 GPIO
  直接接到同轴 RCA 输入。
- 修改输出引脚时避开已使用的 GPIO，例如 UART 日志的 GPIO 0/1、板载 LED
  的 GPIO 25；标准 Pico 的 GPIO 23/24/29 也用于板上功能。

在工程目录执行：

```powershell
cmake -S . -B build -DPICODAC_OUTPUT=SPDIF -DPICODAC_SPDIF_PIN=22
cmake --build build -j 4
```

生成 `build/mdac_adc2.uf2`，通过 BOOTSEL 模式复制到 Pico 即可烧录。
无需额外安装或链接 pico-extras；适配的 PIO 程序已包含在工程中。

切回 I2S：

```powershell
cmake -S . -B build -DPICODAC_OUTPUT=I2S
cmake --build build -j 4
```

I2S 默认 DATA=18、BCLK=16、LRCLK=17。选项会保存在 CMake 缓存中，切换后需重新编译。

## 音频行为

| USB 输入 | SPDIF 输出 |
| --- | --- |
| 16 位 | 16 位有效数据，左对齐至 SPDIF 的 24 位音频区域 |
| 24 位（32 位容器） | 保留全部 24 位有效数据 |
| 32 位 | 取高 24 位，丢弃低 8 位，不加抖动 |

均支持双声道 44.1、48、88.2、96 kHz。沿用 USB 音量、主/左右声道静音和反馈端点。
采样率修改会重置输出和音频缓冲区，再重新缓冲播放。采样率请求仅接受上述四个值。

`spdif_encode.c` 生成完整的 192 帧块，包含 B/M/W 前导码、有效性位、声道状态及偶校验。
声道状态标识消费级线性 PCM，并随输入格式更新采样率和有效位数。
`spdif.c` 使用 PIO0 的一个动态分配状态机、一个 DMA 通道和 DMA IRQ1。
编码完成的缓冲区才交给 DMA；没有新块时发送完整静音块，保持块边界。
停止 USB 音频接口后关闭输出，GPIO 拉低；静止状态不维持接收器锁定。

每帧使用 256 个 PIO 时钟周期。保持工程现有的 92.16 MHz 系统时钟，按
PIO 的 8 位小数分频器就近取整；44.1/88.2 kHz 的平均频率存在约 -98 ppm
的量化误差。48/96 kHz 的平均分频比可精确表示。实际抖动、晶振误差和接收器
锁定情况仍需在硬件上测量。

## 验证

主机编码测试（GCC，不依赖 Pico 硬件）：

```powershell
gcc -std=c11 -Wall -Wextra -Werror -I . spdif_encode.c tests/spdif_encode_test.c -o build/spdif_encode_test.exe
./build/spdif_encode_test.exe
```

覆盖四种采样率 × 三种输入位深，逐帧解码随机/边界 PCM 和静音，检查
前导码、音频位序、有效性、偶校验和完整的声道状态块。

硬件验收建议：

1. 连接 SPDIF DAC，逐个测试上述采样率和 USB 位深，确认锁定和声音正常。
2. 播放仅左/仅右声道信号，确认声道顺序；测试系统音量及静音。
3. 连续执行停止/启动、位深切换及采样率切换，检查无旧音频重放。
4. 用逻辑分析仪验证 192 帧块、输出采样率及播放中断供数据时的静音块。

软件测试和编译不能代替实际的接收器锁定、电气和听音验证。

## 参考源码与许可

- [pico-extras audio_spdif](https://github.com/raspberrypi/pico-extras/tree/52fd7a786ce47c989afeaee67e0c6aebee56ed2d/src/rp2_common/pico_audio_spdif)：
  参考 NRZI/BMC 编码、前导码与 PIO 方案。其音频池接口主要支持 16 位 PCM，
  本工程使用独立的 16/24/32 位输入适配器。`spdif.pio` 保留上游版权声明，
  对应许可见 [LICENSE.pico-extras](LICENSE.pico-extras)。
- [pico-sdk hardware_dma](https://github.com/raspberrypi/pico-sdk/tree/079c6f39023649b154152db30f1d781e884879bc/src/rp2_common/hardware_dma)：
  使用 SDK 的 DMA、PIO、时钟及 IRQ API；本次固件构建使用本机 SDK 2.3.1。
- [pico-examples pio/uart_dma](https://github.com/raspberrypi/pico-examples/tree/0d62f75bafc2c8120d3276c3343d1a9195e909e9/pio/uart_dma)：
  参考 PIO/DREQ 驱动 DMA、共享中断和资源清理方式。
