# Linux 小智业务与 CM0 适配

[返回 README](../README.zh-CN.md) · [架构概览](00-overview.md) ·
[UI 开发规范](01-user-interface.md) · [运行配置](03-runtime-configuration.md)

本文记录交互参考来源与当前实现的适配依据。构建、安装和首次绑定见 README，
连接地址、设备标识、音频设备和 KWS 配置见[运行配置](03-runtime-configuration.md)。

## 参考来源

参考：[100askTeam/xiaozhi-linux](https://github.com/100askTeam/xiaozhi-linux/tree/9eff7f419122d2ba0a7ec70bd669edfc72dd9066)，
固定提交 `9eff7f419122d2ba0a7ec70bd669edfc72dd9066`。以该版本实际源码为依据，
借鉴交互与状态流，使用本项目代码实现，不引入参考项目的 UDP 多进程框架、字体或图片。

协议行为另参考 T-Display K230 `k230_launcher` 的小智实现，基线提交为
`bb831ab358b66f5bd9a87ecd7c580fee4537492e`。本项目的设备构建目标是 CM0 ARM64 Linux，
协议 helper 使用 ALSA；桌面模拟器使用 SDL2 音频。K230 helper 和唤醒资源的兼容路径
不代表 CM0 固件自带这些文件。

## 业务与界面对照

| 参考项目实际行为 | 本项目适配 |
| --- | --- |
| `gui/lv_100ask_xz_ai/src/lv_100ask_xz_ai_main.c`：顶部状态、中央表情、当前一句字幕 | 主界面使用大圆脸、状态与用户/助手字幕；完整聊天记录放在独立面板 |
| `control_center/control_center.cpp`：激活、连接、聆听、说话等状态 | 保留自动 OTA 绑定、6 位验证码、令牌保存、重连与录音失败提示 |
| hello 后自动监听；TTS 时暂停上传，TTS 结束后重新监听 | 连续对话由用户显式开启，以现有本地 VAD 检测停顿；回复实际播放完后再开始下一轮 |
| GUI 点击回调没有实际业务指令，manual/realtime 只有定义 | 提供按住说话、连续模式开关、打断、新会话和重试的真实命令 |
| STT/TTS 文本通过 IPC 显示 | 通过 helper 事件、Model 和 Subject 更新字幕与聊天记录 |
| emotion 字段被读取但尚未完整发送到 UI | 使用已有 emotion 通道驱动 LVGL 原生五官与状态动画 |
| Wi-Fi/电量处理与 IoT 控制部分是占位 | Wi-Fi、电量由 AppKit 系统栏展示；不声明未实现的 IoT 控制能力 |

连续对话采用半双工：本地采集与 VAD → Opus 上传 → 服务端回复 → 播放排空 →
再次采集。线上 `listen` 使用已有单轮协议，和参考的服务端 `auto` 断句实现不同。
这样复用已经实现的录音链路，并避免没有 AEC 时将扬声器声音录回服务器。
关闭连续对话、用户打断或录音失败都会释放录音；重连不会擅自重新开启麦克风。

## 屏幕与布局

仓库锁定的 [AppKit runtime](../third_party/cm0-appkit/src/runtime.cpp) 默认使用
`568 x 1232` 竖屏画布，横屏时交换逻辑宽高为 `1232 x 568`。
[渲染测试](../tests/render_ui.cpp) 使用这两组视口，并覆盖 `320 x 480` 的嵌入式 parent
及其方向切换。这些值是当前源码和测试的布局基线；设备显示模式由实际固件和 DRM 配置决定。

AppKit 的[系统状态栏](../third_party/cm0-appkit/include/cm0/status_bar.h)高度为 96 px。
应用接收状态栏以下的 parent 内容区，并为
屏幕底部 38 px 高、距底边 3 px 的 home indicator 留出 41 px 空间。
因此 `1232 x 568` 横屏的页面布局不能使用完整的 568 px 画布高度。

- 竖屏以表情、字幕和操作纵向排列；宽横屏将表情与字幕并排。
- 结构断点来自 parent 实际可用宽高，不使用 CPU 或固定板型判断。
- 字幕和历史各自可滚动，绑定码及主要按钮保持可见。
- 小尺寸 parent、方向切换和历史阅读位置纳入渲染交互测试。
- 页面不自行旋转或缩放触摸坐标；方向与 evdev 映射由 AppKit 管理。

页面背景、容器、文字和主操作沿用 [UI 开发规范](01-user-interface.md)的语义颜色。
圆脸颜色定义在 [components.hpp](../src/components/components.hpp)，
[AssistantView](../src/pages/assistant/assistant_view.cpp) 使用 LVGL 原生对象绘制五官、
驱动状态动画，文字统一使用 AppKit 字体链。

## CM0 音频

[协议 helper](../src/protocol/xiaozhi_helper.c) 检测到声卡 ID `CM0ES8389` 时，
将录音和播放设备设为 `plughw:CARD=CM0ES8389,DEV=0`，让 ALSA 处理
16 kHz 单声道录音和服务端采样率播放所需的格式转换；其他 ALSA 系统使用 `default`。
可通过 `XIAOZHI_CAPTURE`、`XIAOZHI_PLAYBACK`，或 helper 的 `--capture`、
`--playback` 参数显式指定设备。桌面构建使用 SDL2 音频，不使用 ALSA 声卡路径。

正常回复需要先播放完缓冲再通知 UI 完成；打断时丢弃缓冲，并建立新会话以隔离
旧回复。ALSA 后端使用 `snd_pcm_drain()` 等待播放排空，SDL2 后端等待播放队列清空。
声卡参数配置失败或实际采样率与请求值不同都会报告错误，不能按错误采样率继续编码。

## 设备标识与验证

早期设备验证发现，直接发送 Linux 的 32 位 `machine-id` 会被 OTA 接口拒绝。
当前实现将其转换为标准 UUID，并将 Device ID 和 Client ID 保存到 `device.conf`，
后续绑定、会话和重启复用同一身份，避免网卡 MAC 变化导致设备身份漂移。
数据目录选择和环境变量覆盖规则见[运行配置](03-runtime-configuration.md)。

可重复的回归覆盖包括：

- [helper 标识测试](../tests/test_helper_identity.c)：UUID 格式、稳定派生和显式身份覆盖。
- [会话控制器测试](../tests/test_session_controller.cpp)：身份持久化、绑定恢复、录音失败、
  连续对话、打断和重连。
- [协议测试](../tests/test_protocol_session.py)：本地 WebSocket 会话和模拟音频链路。
- [渲染测试](../tests/render_ui.cpp)：系统栏安全区、主界面、聊天记录、绑定码及方向切换。

上述测试使用本地或模拟环境。设备验收仍需检查目标板的录音、扬声器播放、网络绑定和
线上语音会话；测试通过不能替代实机验证。执行方式见 [README](../README.zh-CN.md)。
