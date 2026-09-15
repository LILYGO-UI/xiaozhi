# LILYGO XiaoZhi 架构总览

本应用是基于 C++17、LILYGO UI AppKit 和 LVGL 9.5 的小智协议语音助手客户端。
运行目标为 CM0 ARM64 Linux，同时提供 SDL2 主机模拟器用于开发和测试。

项目介绍、环境搭建和构建命令见 [中文 README](../README.zh-CN.md)；
布局与资源约束见 [UI 开发规范](01-user-interface.md)；
首次绑定、运行参数、数据路径和音频排障见[运行配置](03-runtime-configuration.md)。

## 模块边界

以下路径均相对于仓库根目录。

| 模块 | 责任 |
| --- | --- |
| `src/app.cpp` | 接收 AppKit 的应用内容区，创建页面及 ViewModel，并按依赖顺序关闭应用。 |
| `src/pages/assistant/assistant_model.*` | 不依赖 LVGL 的会话状态、绑定码校验、字幕消息、表情和聊天记录规则。 |
| `src/pages/assistant/assistant_view_model.*` | 将用户命令交给控制器，消费 `SessionEvent` 更新 Model，并通过 Subject 发布展示状态。 |
| `src/pages/assistant/session_controller.*` | 设备身份和连接配置、helper 子进程、FIFO 控制、事件解析、KWS 与重连生命周期。 |
| `src/pages/assistant/assistant_view.*` | LVGL 控件、响应式布局、表情、字幕和历史面板，转发触摸与硬件键输入。 |
| `src/pages/assistant/hardware_mic_input.*` | 发现 Linux input 设备，非阻塞读取标准 MIC/录音键，并在析构时关闭设备描述符。 |
| `src/components/` | 语义颜色、通用 LVGL 创建函数及 Subject RAII 封装。 |
| `src/protocol/xiaozhi_helper.c` | 独立进程中的 OTA 绑定、WebSocket、TLS、Opus 及 ALSA/SDL2 音频。 |

当前应用只有一个页面，没有全局路由模块。页面私有 Model 与 ViewModel、View
位于同一目录，但纯 Model 编译到独立的 core 目标，不链接 LVGL。

## 运行时数据流

```text
触摸 / MIC 键 -> AssistantView -> AssistantViewModel -> SessionController
                                                          |
KWS helper -> 唤醒事件 ------------------------------------+
                                                          |
                                                       FIFO 命令
                                                          v
                                   协议 helper <-> WebSocket 服务 / 音频设备
                                                          |
                                                     标准输出事件
                                                          v
AssistantView <- Subject <- AssistantViewModel <- SessionController.poll()
                                   |
                            AssistantModel 更新
```

页面通过 160 ms 的 LVGL timer 轮询硬件键与 helper 结果，用户命令也会立即触发
ViewModel 的 `tick()`。这些调用都在 UI 主线程执行；控制器读取的是非阻塞管道，
协议 helper 负责网络和音频工作，不调用 LVGL。

控制器将日志转换为结构化 `SessionEvent`。ViewModel 消费事件更新 Model，再发布
Subject；View 根据 Subject 和 Model revision 刷新字幕、聊天记录与表情。

## 状态机

应用状态覆盖启动、绑定、连接、待命、录音、思考、播放、重连、服务不可用及错误。
界面中的 `Ready to talk` 表示会话就绪；真实 PTT 在收到 helper 的
`capture_started` 确认后才显示录音状态。Model 对消息去重并保留最多 80 条记录，
绑定码必须为 6 位数字。

helper 解码 JSON 的 Unicode 转义，保持完整 UTF-8 字符，并将每条日志限制在一行。
控制器按日志级别和事件前缀解析；普通字幕或诊断 JSON 中出现内部状态文字不会
触发会话切换，也不会作为连接配置保存。

会话异常退出后，重连等待为 2、4、6 秒，逐次递增，最多 20 秒；成功收到
`session ready` 后退避计数清零。用户点击重试会立即终止旧进程、重新探测 helper
和读取连接配置，再执行绑定或会话路径。

## 音频所有权

同一时刻只有 PTT、连续对话采集或 KWS 之一持有麦克风。连续对话使用本地 VAD
自动断句，等实际播放排空后再开始下一轮；用户关闭、打断或发生错误时停止自动采集。
开始 PTT 前会停止 KWS；回复中按下 PTT 会中止旧轮次并准备新会话，提前松手取消
待启动的录音。退出应用释放所有音频资源。KWS helper/模型不存在时，PTT、连续对话
和标准硬件 MIC 键仍可用。

打断通过 FIFO 发送 `ABORT`，丢弃旧回复的待播放音频，并重新建立协议会话。
控制器在等待旧 helper 退出时不再消费该轮次的日志；若 2 秒内未退出，则回收旧进程
再创建新会话。连续对话属于半双工流程，不提供播放期间的全双工 AEC。

## 销毁顺序

`src/app.cpp` 持有 ViewModel 和 View。关闭应用时先调用 `AssistantView::destroy()`，
然后销毁 View，最后销毁 ViewModel，确保 LVGL 绑定不会访问已释放的 Subject。

1. View 删除轮询 timer，阻止新的硬件键和会话轮询。
2. ViewModel 停止会话；控制器关闭连续模式、发送 `QUIT`、停止 KWS、回收协议子进程，
   关闭管道和控制描述符，并删除 FIFO。
3. View 删除顶层 `surface`，由 LVGL 一并删除子控件与对象绑定的 observer，随后
   清空缓存指针和消息行记录。
4. View 析构时关闭硬件输入描述符；ViewModel 随后释放状态及 Subject。

`SessionController` 析构时也会调用可重复执行的 `stop()`。应用关闭须经过上述
生命周期入口，不能以外部删除 `surface` 代替停止会话。

## 响应式界面

主界面以状态、大表情和当前字幕组织对话，竖屏纵向排列，横屏将表情与字幕并排。
字幕可滚动，完整聊天记录在应用 surface 下的历史面板中展示；打开/关闭面板保留
会话状态与阅读位置。布局依据 parent 内容区，并为底部系统手势指示器预留 41 px。
界面与业务参考来源及目标板尺寸依据见 [Linux 小智适配说明](02-linux-reference.md)。

## 持久化与安全

设备身份与连接配置分开持久化。设备身份在首次请求前保存，后续激活、会话与重启
复用同一标识；绑定返回的 token 仅在绑定 helper 成功退出后落盘。配置写入使用
同目录临时文件与 rename，数据目录权限为 `0700`、配置文件权限为 `0600`。
token 只通过 fork 后的环境传给 helper，不出现在进程参数列表和可见日志中。
线上连接启用 TLS 证书及主机名校验。

桌面模拟器使用同一个协议 helper，音频后端为 SDL2；默认执行真实 OTA 绑定和
WebSocket 会话，设备构建使用 ALSA。离线模拟会话仅在桌面环境显式启用。
目录选择、环境变量及本地音频检查命令统一见[运行配置](03-runtime-configuration.md)。

## 失败策略

| 场景 | 行为 |
| --- | --- |
| 无法创建数据目录或读取、保存设备身份 | 显示错误，停止当前启动流程；修复路径或权限后可手动重试。 |
| 未找到协议 helper | 显示服务不可用，不进入绑定或会话；重试会重新探测 helper。 |
| OTA 失败或绑定程序异常退出 | 显示连接错误或重连状态，不保存本次返回的 token，并按退避时间重试。 |
| 会话断开、控制命令失败或音频出错 | 关闭连续模式、停止 KWS 和当前轮次，建立新会话；重连不会自动重新开启连续录音。 |
| 录音设备打开、预热或读取失败 | 保留具体麦克风诊断及聊天记录中的提示，避免被重连倒计时覆盖。 |
| KWS helper 或模型缺失 | 保留 PTT、连续对话和硬件 MIC 键入口。 |

聊天记录保存在本次应用运行的内存中；关闭历史面板或切换方向不会清空记录。
新会话命令清空记录并重新建立协议会话，不删除已保存的绑定信息。

## 验证

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

主机测试覆盖 Model、ViewModel、绑定与身份持久化、会话恢复、PTT、连续模式、打断、
音频采集和本地 WebSocket 协议。渲染器模拟 AppKit 系统栏与应用 parent，生成主界面、
聊天记录和绑定画面的竖屏、横屏及嵌入式父容器快照，并检查方向切换后的状态保留。

完整测试清单、快照文件名与视觉验收要求见 [UI 开发规范](01-user-interface.md#验收)。
自动化测试使用本地服务和模拟音频，不需要真实绑定或麦克风权限；设备麦克风、扬声器
及线上服务仍需在目标环境验证。涉及 SDK、工具链或安装打包变更时，还须完成
`cm0-cross` 构建和 Debian 打包验证。
