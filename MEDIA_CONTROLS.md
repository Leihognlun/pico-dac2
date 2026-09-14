# 媒体按键与 LED（固件 1.08）

在 1.03 音频功能基础上增加控制面板，不包含 E-AC-3 实验功能。

1.08 修正 EP0 控制写入完成后错误准备 OUT 状态包的问题，并在新 SETUP
到来时取消旧 EP0 缓冲、忽略旧完成事件。控制读取仅在实际响应短于主机请求
且长度为 64 的倍数时补终止零长度包。需在 CM4 上验证连续命令。

| 功能 | 按键 GPIO | LED GPIO |
| --- | --- | --- |
| 播放/暂停 | 10 | 9 |
| 音量加 | 13 | 12 |
| 音量减 | 27 | 26 |

按键另一端接 GND，固件启用内部上拉，低电平为按下，20 ms 消抖。
LED 高电平点亮，上电默认全灭。LED 应串联合适的限流电阻；GPIO 20 不使用。
LED 状态只由上位机设置，按键不会自动切换 LED。USB 重新配置保留 LED 状态，
MCU 重启后熄灭。
LED− GPIO 11、14、28 初始化为低电平输出。

三个按键通过标准 Consumer Control HID 控制主机，使用键码 Play/Pause
（0xCD）、Volume Increment（0xE9）、Volume Decrement（0xEA）。按下和释放
均上报，支持同时按键；长按音量是否连发由主机处理。按键无需 Python 程序运行。
HID 每 10 ms 轮询一次，消抖在 HID 传输回调中执行，不阻塞音频处理。

## Python 控灯

安装依赖并设置三个灯（顺序固定为 GPIO 9、12、26）：

```sh
python -m pip install hidapi
python tools/comm.py --leds 1 0 1
python tools/comm.py --leds 0 0 0
python tools/comm.py --leds 1 0 1 --verify
```

`1` 点亮，`0` 熄灭，每次写入完整的三个灯状态。
1.07 默认使用 EP0 Feature Report 控灯，发送前后同步读取状态和命令计数。
只有 LED 状态匹配且计数恰好增加 1（支持 32 位回绕）才确认成功，
避免旧状态相同但命令未执行的误判。CLI 总是验证，保留 `--verify` 兼容旧命令。
确认的是软件设置状态，不是 LED 光学状态。请同时更新固件与脚本。
原 interrupt OUT 在 CM4 实机上出现仅首条命令生效，原因尚未实机定位；
`--transport interrupt` 仅用于诊断，日常灯控默认走 feature。

Python API 可导入
`tools.comm.set_leds(device, (1, 0, 1))`，其中 device 为已打开的 hidapi 设备。
默认 VID/PID 为 CAFE/BABE，可用 `--vid`、`--pid` 修改，多设备可用 `--path`。
脚本选择 Usage Page FF00 / Usage 01 的厂商集合，避免打开系统媒体键集合。
Linux 需要对应 hidraw 的读写权限。
Linux 若 hidapi 未提供 Usage 信息，脚本按本工程 HID 接口号 2 选择设备，
同一路径的多个集合会去重。`python tools/comm.py --list` 可查看匹配设备的
路径、接口号、Usage 和固件版本；列表为空则检查 USB 连接及 VID/PID。
若检测到早于 1.07 的固件，脚本会提示先烧录再重新连接。

## HID 协议

同一 HID 接口包含厂商集合和 Consumer Control 集合。报告的首字节是 Report ID：

| ID | 方向 | 数据（不含 ID） |
| --- | --- | --- |
| 1 | IN | 原有 16 字节 `AD` 音频诊断，总长度 17 字节 |
| 2 | IN | 1 字节按键状态：bit0 播放/暂停，bit1 音量加，bit2 音量减 |
| 3 | OUT | 1 字节 LED 状态：bit0 GPIO9，bit1 GPIO12，bit2 GPIO26 |
| 4 | Feature / EP0 | 1 字节 LED mask + 4 字节 little-endian 累计命令数 |

默认 Feature SET_REPORT：wValue=0x0304，数据 `04 mask 00 00 00 00`，
wLength=6。GET_REPORT 使用相同 wValue，返回 `04 mask count[4]`。
每条合法命令（即使状态不变）均增加计数；无效命令不增加计数。
Python 使用 `send_feature_report()` / `get_feature_report()`，不从 IN 队列取旧诊断。

LED 命令例如 `03 05` 表示 GPIO9/26 点亮、GPIO12 熄灭。高 5 位必须为零。
支持 interrupt OUT，也支持 EP0 SET_REPORT（wValue=0x0203，wLength=2，
数据仍为 `03 mask`）。GET_REPORT 可读取 ID 2 的按键状态与 ID 3 的 LED 状态。
hidapi 的 `write()` 首字节就是报告 ID，不要再增加零字节。

`audio_monitor.py` 兼容旧版 16 字节诊断和新版带 ID 的诊断，忽略媒体键报告。
USB bcdDevice 更新为 0x0108；烧录后重新插拔。
若系统仍缓存旧 HID 描述符，移除旧设备实例后重新连接。

## 验证

```sh
python tests/run_tests.py
python tests/media_controls_test.py
cmake --build build
```

主机测试覆盖 GPIO 初始化、消抖、组合按键及释放、LED 报告校验、
SET_REPORT/GET_REPORT、空闲周期、USB 描述符及原有音频回归。
实机需验证：三个键的主机媒体响应、三个 LED 的独立控制、持续播放时按键和控灯。

协议参考：[USB-IF HID Usage Tables](https://www.usb.org/sites/default/files/hut1_3_0.pdf)、
[hidapi API](https://libusb.info/hidapi/group__API.html)。
