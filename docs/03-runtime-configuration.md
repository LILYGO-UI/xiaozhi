# 运行配置与故障排查

[English README](../README.md) · [中文 README](../README.zh-CN.md) ·
[架构概览](00-overview.md) · [UI 开发规范](01-user-interface.md) ·
[Linux 参考与 CM0 适配](02-linux-reference.md)

本文说明已构建应用的绑定、运行参数、数据保存和音频行为。开发环境、编译与安装步骤见
[中文 README](../README.zh-CN.md)。界面控件使用英文，用户转写与助手回复保留服务端原文。

## 首次绑定与会话恢复

1. 将设备连接到网络，打开 `XiaoZhi`；桌面模拟器默认也使用真实服务。
2. 在 [xiaozhi.me](https://xiaozhi.me) 控制台选择“添加设备”。
3. 应用向 OTA 接口请求绑定信息，在界面显示 6 位验证码。
4. 在网页输入验证码。绑定成功后，应用保存服务端返回的 WebSocket 地址与令牌，随后连接会话。

应用以保存的 Device ID 和 Client ID 完成绑定与后续连接。只有绑定 helper 成功退出且返回
非空令牌时才保存连接配置；下次启动读取已有令牌，不会重复要求输入验证码。

会话异常退出后，应用按 2、4、6 秒递增等待，最多等待 20 秒再重试；连接成功后清零
退避计数。点击 `Reconnect` 可以立即重新探测 helper、读取配置并重试。点击 `History`
中的 `New chat` 会清空本次聊天记录并创建新的协议会话，保留设备身份和绑定配置。
显示验证码时，重试按钮名称为 `Refresh code`。

### 默认服务与协议

| 用途 | 默认地址或行为 |
| --- | --- |
| 设备绑定控制台 | `https://xiaozhi.me` |
| OTA 检查 | `POST https://api.tenclass.net/xiaozhi/ota/` |
| 激活轮询 | 在 OTA 地址末尾追加 `activate`，默认 `POST https://api.tenclass.net/xiaozhi/ota/activate` |
| WebSocket 初始地址 | `wss://api.tenclass.net:443/xiaozhi/v1/`；绑定后使用 OTA 返回的地址 |
| 会话鉴权 | `Protocol-Version: 1`、`Device-Id`、`Client-Id` 与 Bearer 令牌 |
| 语音上传 | 16 kHz、单声道、60 ms 帧长的 Opus |
| 服务端消息 | `hello`、STT、TTS、LLM emotion；录音通过 `listen/start` 与 `listen/stop` 控制 |

真实会话需要网络、可用的 DNS 和 CA 证书。录音上传到配置的语音服务，由服务返回转写、
文字回复和语音；绑定请求也会发送设备标识。服务端的数据保留行为以所用服务的政策为准。

## Helper 选择

主应用使用独立 helper 处理协议和音频，按下列顺序选取第一个可执行路径：

1. 非空 `LILYGO_XIAOZHI_HELPER`。
2. 模拟器构建记录的 `lilygo-ui-xiaozhi-helper` 路径，仅桌面构建使用。
3. `/usr/libexec/lilygo-ui-xiaozhi-helper`，由本项目设备包安装。
4. `/root/app/k230_phone_ui/k230_xiaozhi_probe`，用于兼容已有 K230 固件。

指定路径不存在或不可执行时，探测会继续尝试后续路径。自定义 helper 必须兼容本项目的
激活输出、会话事件和 FIFO 控制协议；接口见
[`session_controller.cpp`](../src/pages/assistant/session_controller.cpp) 与
[`xiaozhi_helper.c`](../src/protocol/xiaozhi_helper.c)。CM0 固件不提供最后一项 K230 helper。

KWS 为可选能力，本项目设备包不包含唤醒 helper 或模型。唤醒 helper 依次探测
`LILYGO_XIAOZHI_KWS_HELPER`、`/usr/libexec/lilygo-ui-xiaozhi-kws`、
`/root/app/k230_phone_ui/k230_xiaozhi_kws`；模型依次探测
`LILYGO_XIAOZHI_KWS_MODEL`、`/usr/share/lilygo-ui-xiaozhi/xiaozhi_kws.kmodel`、
`/root/app/k230_phone_ui/models/xiaozhi_kws.kmodel`。两者都可用时才启用唤醒入口。

## 环境变量

在启动应用前设置变量。使用已经构建的模拟器时，例如：

```sh
LILYGO_XIAOZHI_DATA_DIR="$HOME/.local/share/xiaozhi-test" \
  ./build/host-simulator/lilygo-ui-xiaozhi
```

| 变量 | 生效范围与含义 |
| --- | --- |
| `LILYGO_XIAOZHI_DATA_DIR` | 应用数据目录的完整路径，覆盖设备与模拟器的默认目录。目录需可写。 |
| `LILYGO_XIAOZHI_HELPER` | 优先尝试的协议 helper 可执行路径。 |
| `LILYGO_XIAOZHI_KWS_HELPER` | 优先尝试的唤醒 helper 可执行路径。 |
| `LILYGO_XIAOZHI_KWS_MODEL` | 优先尝试的可读 KWS 模型路径，需与唤醒 helper 匹配。 |
| `LILYGO_XIAOZHI_MIC_INPUT` | Linux 硬件 MIC 键的输入设备，例如 `/dev/input/event3`；设置后只读取该设备。未设置时扫描 `/dev/input/event*`。 |
| `LILYGO_XIAOZHI_MOCK` | 仅模拟器识别，值必须恰为 `1` 才启用离线模拟会话；不连接服务、不采集真实音频。 |
| `XIAOZHI_OTA_URL` | helper 使用的 OTA 接口，默认见上表；用于兼容的自建服务，已有令牌时不会重新执行 OTA 绑定。 |
| `XIAOZHI_CAPTURE` | helper 的录音设备。Linux 使用 ALSA 设备名；模拟器使用 SDL2 设备名，`default` 表示默认输入。 |
| `XIAOZHI_PLAYBACK` | helper 的播放设备。Linux 使用 ALSA 设备名；模拟器使用 SDL2 设备名，`default` 表示默认输出。 |
| `XIAOZHI_DEVICE_ID` | 原生设备应用本次运行的 Device ID 覆盖值，不改写保存的身份；模拟器应用忽略此覆盖。 |
| `XIAOZHI_CLIENT_ID` | 原生设备应用本次运行的 Client ID 覆盖值，不改写保存的身份；模拟器应用忽略此覆盖。 |

`XIAOZHI_URL` 和 `XIAOZHI_TOKEN` 是直接调用 helper 时的参数来源。通过应用启动时，
控制器会用自身读取的连接配置覆盖这两个变量，因此它们不能覆盖应用已保存的地址和令牌。
直接调用 helper 的其他参数见 `lilygo-ui-xiaozhi-helper --help`；进行网络诊断时需显式带上
`--tls-verify`，应用启动 helper 时会自动传入此参数，以验证 TLS 证书与主机名。

### 离线预览

```sh
LILYGO_XIAOZHI_MOCK=1 lpm start --foreground
```

该模式用于交互与布局预览，返回固定的本地示例会话，不验证真实绑定、网络或麦克风。
正常启动不设置该变量。设备构建不启用此模拟模式。

## 数据目录与设备身份

| 优先级 | 条件 | 数据目录 |
| --- | --- | --- |
| 1 | 非空 `LILYGO_XIAOZHI_DATA_DIR` | 该变量指定的目录 |
| 2 | 桌面模拟器 | 构建时记录的项目目录下 `.xiaozhi/`，与启动时的工作目录无关 |
| 3 | 设备设置了非空 `XDG_DATA_HOME` | `$XDG_DATA_HOME/lilygo-ui-xiaozhi` |
| 4 | 设备设置了非空 `HOME` | `$HOME/.local/share/lilygo-ui-xiaozhi` |
| 5 | 设备没有以上环境变量 | `/tmp/lilygo-ui-xiaozhi` |

数据目录包含：

| 文件 | 内容与生命周期 |
| --- | --- |
| `connection.conf` | `url=` 和 `token=` 两行连接配置；绑定成功后保存。 |
| `device.conf` | Device ID 和 Client ID，各占一行；首次请求前创建，重启后复用。 |
| `session.ctl` | 会话期间的控制 FIFO；正常关闭应用时移除。 |

应用创建或访问数据目录时设为 `0700`，新写入的身份、令牌文件和 FIFO 使用 `0600`。
持久文件先写入同目录临时文件，经 `fsync` 后通过 `rename` 替换。项目 `.xiaozhi/`
已列入 Git 忽略规则。令牌通过子进程环境传给 helper，不放入应用的命令行参数和可见
UI 日志；直接运行 `activate` helper 会输出包含令牌的配置行，不应把完整输出公开。

设备首次创建身份时，优先沿用 `eth0`、`wlan0`、`usb0` 中读取到的 MAC，并将合法的
`/etc/machine-id` 转换为标准 UUID；缺失时生成随机标识。模拟器生成并保存自己的标识。
之后一直复用 `device.conf`，避免 USB 网卡 MAC 在重启后改变而造成绑定身份变化。
原始 32 位 machine-id 不是此接口需要的标准 UUID 格式。

更换数据目录会使用该目录中的身份与令牌，空目录会触发独立的首次绑定。`Reconnect`
不会删除失效令牌；需要重新走绑定时，先退出应用，备份并移走对应的 `connection.conf`，
保留 `device.conf` 后重启。不要把已绑定的数据目录复制给另一台设备共用身份。

聊天记录仅保存在本次应用会话的内存中，最多保留 80 条，每条文本最多保留 1024 字节并
保持 UTF-8 边界。打开或关闭 `History`、切换方向不会清空记录；`New chat` 或退出应用会清空。

## 音频与唤醒

### 语音入口

- 按住 `Hold to talk` 开始录音，显示 `Release to send` 后松开发送。连接尚未准备好时会等待，
  若在录音开始前松开则取消待录音操作。
- 开启 `Continuous chat` 后，本地 VAD 检测语音与停顿并自动发送，回复播放排空后开始下一句。
  关闭连续模式、点击 `Stop`、录音或连接失败都会停止连续采集；重连后需重新开启。
- 思考或回复时点击 `Stop` 中止当前轮次；回复时按住麦克风会打断旧回复，在新协议会话准备好后录音。
- Linux 的 `KEY_RECORD`、`KEY_MICMUTE` 和受系统头文件支持的 `KEY_VOICECOMMAND` 均按
  “按下录音、松开发送”处理，自动重复事件会被忽略。进程需有读取相应 `/dev/input/event*` 的权限。

采集与播放采用半双工：录音、上传、回复、播放排空、再录音。应用没有全双工 AEC，
不提供一边播放回复一边识别唤醒词的能力。连续对话不依赖 K230 唤醒模型。

### 音频设备

设备 helper 使用 ALSA。检测到 `CM0ES8389` 声卡时，录音和播放默认使用
`plughw:CARD=CM0ES8389,DEV=0`，由 ALSA 转换声卡格式；其他 Linux 声卡默认使用 `default`。
可以用 `XIAOZHI_CAPTURE`、`XIAOZHI_PLAYBACK` 覆盖，也可以在直接调用 helper 时使用
`--capture`、`--playback` 参数。

模拟器 helper 使用 SDL2 采集和播放，需允许启动它的终端或应用使用麦克风，并配置好系统
默认输入与输出。以下命令仅检查本地采集、Opus 编解码与回放，不连接语音服务：

```sh
./build/host-simulator/lilygo-ui-xiaozhi-helper audio-loopback --seconds 1
```

在设备上使用 `/usr/libexec/lilygo-ui-xiaozhi-helper audio-loopback --seconds 1`。
运行回环测试前退出 XiaoZhi，释放麦克风和播放设备。

### KWS 生命周期

兼容的唤醒 helper 和模型都存在时，应用首次连接成功后可监听 `Xiaozhi Xiaozhi`。
KWS 只在应用打开且没有录音或播放时持有麦克风，按住说话或连续采集开始前会停止 KWS。
资源不存在时仍可使用按住说话、连续对话和硬件 MIC 键。

当前实现中，连续模式、打断、`New chat`、重连或会话错误会暂停本次应用生命周期的自动
KWS。需要恢复自动唤醒时关闭并重新打开应用。仅提供 K230 模型文件并不能让 CM0 执行唤醒，
还需要可在目标设备运行并兼容该模型的 helper。

## 常见运行问题

| 现象 | 检查与处理 |
| --- | --- |
| 无法获取验证码 | 检查 DNS、网络、系统时间、CA 证书和 OTA 服务；自定义 `XIAOZHI_OTA_URL` 时核对兼容性，然后点击 `Reconnect`，已有验证码时使用 `Refresh code`。 |
| 绑定后重启仍需验证码 | 检查应用启动用户与数据目录是否改变，以及目录写权限；绑定 helper 失败退出时不会保存令牌。 |
| 一直自动重连 | 检查网络与服务状态；`Reconnect` 会复用现有身份和令牌。服务已撤销绑定时按数据目录章节重新绑定。 |
| `Xiaozhi protocol helper not found` | 检查 helper 是否存在且可执行；设备可用 `dpkg -L lilygo-ui-xiaozhi` 检查安装内容，用 `ldd /usr/libexec/lilygo-ui-xiaozhi-helper` 检查缺失的动态库。安装完整包后重新打开应用或点击 `Reconnect`。 |
| `Cannot create or access the pairing data folder` | 检查运行用户对数据目录及父目录的访问、创建与权限修改能力；可设置可写的 `LILYGO_XIAOZHI_DATA_DIR` 后重启。 |
| `Cannot read or save the device identity` | 检查 `device.conf` 是否包含两行非空身份、文件是否可读以及目录是否可写；保留原身份备份，避免意外创建新设备身份。 |
| 录音失败或没有声音 | 检查麦克风权限、默认输入与输出、音量、设备是否被其他进程占用；退出应用后执行音频回环测试。 |
| 硬件 MIC 键无响应 | 检查设备上报的按键码与进程读取权限；确认 `LILYGO_XIAOZHI_MIC_INPUT` 指向当前输入设备，修改后重启应用。 |
| 连续对话在错误后关闭 | 错误恢复不会自动重新开启麦克风，连接恢复后手动开启 `Continuous chat`。 |
| 没有唤醒词入口或唤醒不再生效 | 检查兼容的 KWS helper 与模型，以及上文 KWS 生命周期；可继续使用触摸或硬件 MIC 键。 |
