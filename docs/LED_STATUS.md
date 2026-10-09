# 板级状态灯与错误码

两种板子的 LED_Green 都是 GPIO25，高电平点亮。固件正常运行时常亮；
ARC 未连接、播放停止、正在缓冲、文件正常结束都不算代码错误。
LED_Yellow 显示 ARC 连接和播放选择：未建立链路时亮 1 秒、灭 1 秒；已连接但未选择
播放时熄灭；已连接且对应端口被选择播放时常亮。停止或切走后熄灭，断开后恢复闪烁。
链路条件是对应端口 HDMI 5V 存在、CEC 注册成功且 ARC 协商完成。

| 板子 | ARC1 LED_Yellow | ARC2 LED_Yellow | LED_Green |
| --- | --- | --- | --- |
| C1 | GPIO15 | 无 | GPIO25 |
| C2 | GPIO17 | GPIO16 | GPIO25 |

## 绿灯错误次数

次数按“亮、灭状态总数”定义，**8 次 = 亮 4 次 + 灭 4 次**。

| 错误码 / 每轮状态数 | 错误来源 | 每轮亮灭次数 |
| --- | --- | --- |
| 0 | 无错误 | 常亮 |
| 2 | 固件调用 `panic`，例如资源申请失败、内部 USB 事件队列满 | 亮 1 次、灭 1 次 |
| 4 | USB 音频缓冲欠载，进入 `STATE_STALLED` | 亮 2 次、灭 2 次 |
| 6 | 任一 CEC 端口检测到 TV 逻辑地址冲突 | 亮 3 次、灭 3 次 |
| 8 | TF 配置/挂载/打开/读取失败，或音频格式不支持、文件损坏/截断等负错误返回 | 亮 4 次、灭 4 次 |

同时存在多个错误时显示优先级：panic > TF/文件 > USB 欠载 > CEC 地址冲突。
清除一种错误不会清除其他来源。可恢复错误消失后显示剩余错误，全部消失后恢复常亮。
TF 错误重试或切歌成功后恢复；普通 EOF 不触发错误码。panic 锁存到复位。

错误开始时先熄灭并等待一轮间隔，使正常常亮之后的第一个亮脉冲清晰可见。
每个亮/灭状态默认持续 250 ms，一轮的所有亮灭状态完成后，额外熄灭等待 2 秒再重复。
持续上报同一个错误不会重新开始计时；改变错误码会从熄灭间隔重新开始。

## 配置

```powershell
cmake -S . -B build/dev-zero-c2-tf -DPICODAC_BOARD=c2 -DPICODAC_INPUT=SD_AUDIO -DPICODAC_ERROR_LED_GAP_MS=2000 -DPICODAC_ERROR_LED_STEP_MS=250
cmake --build build/dev-zero-c2-tf
```

`PICODAC_ERROR_LED_GAP_MS` 配置轮间隔，`PICODAC_ERROR_LED_STEP_MS` 配置单次亮或灭的时长，
单位毫秒，允许 1..60000。错误次数定义在 `src/board_status.h` 的 `board_error_t` 中。

正常主循环通过 `board_status_task()` 更新灯，不使用阻塞延时或 PIO。
TF Core1 的错误通过已有原子状态交给 Core0 显示。
`board_error_code` 可在调试器中查看当前显示码。
`src/board_panic.c` 使用 Pico SDK 的自定义 panic 入口，在故障上下文自行循环闪码，
不依赖主循环、中断回调或 stdio 锁；原始格式字符串保存在 `board_panic_message`。
此入口不再进行默认 panic 的格式化串口打印，也不宣称捕获所有 HardFault 或断电故障。
# 播放按钮的待机灯配置

在 `boards/c1.h` 中配置 B1；在 `boards/c2.h` 中分别配置 B1、B2。
C1 的 B2 是跳过键，不增加待机效果。

```c
#define BOARD_B1_IDLE_LED_MODE 0          // 0：固定亮度；1：呼吸
#define BOARD_B1_IDLE_LED_PERIOD_MS 2000  // 一次完整渐亮/渐暗周期，500..5000 ms
#define BOARD_B1_IDLE_LED_BRIGHTNESS 0    // 固定亮度 0..100%；呼吸时忽略
```

C2 的 B2 使用 `BOARD_B2_IDLE_LED_*`。默认固定亮度 0%，即熄灭。
呼吸从暗开始，半周期达到全亮，再渐暗。采用余弦渐变叠加 Gamma 2.2
校正：占空比为 `((1-cos(2πt/T))/2)^2.2`。129 点半周期查表加整数插值，
无运行时浮点三角函数；最暗和最亮处过渡更柔和。固定亮度仍直接对应占空比。
硬件 PWM 调光，不阻塞音频；
非法模式或越界参数会导致编译失败。修改后重新编译对应板型。

C2 TF/USB 仅在没有选择播放端口时使用待机效果；播放时保持原来的选中灯亮、
未选中灯灭逻辑。C1 TF 在停止时使用 B1 待机效果，上电自动播放行为不变。
C1 USB 在音频流未启动时使用 B1 待机效果，启动后恢复主机 HID 灯状态；
待机期间主机设置的 B1 灯状态暂存，B2 等其他灯仍按原逻辑处理。
ARC 黄灯和错误绿灯不受影响。
