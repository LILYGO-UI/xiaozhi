# LILYGO XiaoZhi

[English](README.md) | **简体中文**

LILYGO XiaoZhi 是面向 LILYGO UI 的语音助手客户端，基于 C++17、
[LILYGO UI AppKit](https://github.com/LILYGO-UI/cm0-appkit) 和 LVGL 9.5 构建。
应用通过 WebSocket 连接兼容小智协议的服务，使用 Opus 传输语音。

项目提供使用 ALSA 音频的 CM0 ARM64 Linux 设备构建，以及用于开发和测试的
SDL2 桌面模拟器。界面控件使用英文；语音转写与助手回复保留服务端返回的语言。

## 预览

<p>
  <img src="assets/previews/xiaozhi-main.png" alt="小智表情与实时字幕" width="240">
  <img src="assets/previews/xiaozhi-binding.png" alt="六位设备绑定码" width="240">
  <img src="assets/previews/xiaozhi-history.png" alt="聊天记录" width="240">
</p>

这些预览由离线测试生成。渲染测试也覆盖横屏和嵌入式布局。

## 功能

- **设备绑定：** 自动通过 OTA 获取六位验证码，在 [xiaozhi.me](https://xiaozhi.me)
  完成绑定后保存连接信息和设备身份。
- **语音对话：** 支持触摸按住说话、可选的本地语音活动检测（VAD）连续对话，
  以及标准 Linux 麦克风硬件键。
- **会话控制：** 打断回复、阅读当前字幕或完整的本次聊天记录，以及开启新会话。
- **表情：** 原生 LVGL 面部表情随录音、播放状态及服务端 emotion 事件变化。
- **连接恢复：** 会话退出后自动重试，等待时间从 2 秒递增至 20 秒，也可手动重连。
- **响应式界面：** 适配 `568 x 1232` 竖屏、`1232 x 568` 横屏及较小的父容器，
  并为系统界面预留空间。

真实对话需要网络连接、兼容的服务及可用的音频输入输出设备。连续对话采用半双工：
回复音频播放完毕后才恢复麦克风采集。可选的唤醒词功能需要兼容的 KWS 资源；
CM0 不自带 K230 唤醒组件，也不提供全双工声学回声消除（AEC）。功能限制与可用设置见
[运行配置](docs/03-runtime-configuration.md)。

## 首次对话

1. 将设备连接到网络并打开 **XiaoZhi**，或按下文说明启动桌面模拟器。
2. 在 [xiaozhi.me](https://xiaozhi.me) 控制台选择 **Add device**（添加设备）。
3. 输入应用显示的六位验证码。应用会自动从官方 OTA 服务获取验证码。
4. 绑定完成后，应用保存返回的 WebSocket 地址和令牌，并连接语音会话。
   后续启动会复用已保存的连接。

| 控件 | 行为 |
| --- | --- |
| **Hold to talk** | 按住麦克风按钮录音，显示 **Release to send** 时松开发送。 |
| **Continuous chat** | 开启后使用本地 VAD，在每句话停顿后发送，回复结束后继续聆听；关闭可停止自动聆听。 |
| **Stop** | 打断当前轮次。回复期间按住麦克风也会打断并准备新的录音；录音开始前松手会取消该次录音。 |
| **History** / **New chat** | 查看本次聊天记录，或清空记录并创建新的协议会话。关闭历史面板会保留其中的内容。 |
| **Reconnect** / **Refresh code** | 重试连接或重新获取绑定码。 |

录音或连接出错时会关闭连续对话，准备好后需重新开启。硬件键和唤醒词行为详见
[运行配置](docs/03-runtime-configuration.md)。

## 开发环境搭建

项目标识、版本和软件包元数据由 [`lpm.toml`](lpm.toml) 提供。
[`CMakePresets.json`](CMakePresets.json) 定义了 `host-simulator` 和 `cm0-cross`。
所有项目命令均在仓库根目录执行。

### 获取源码

```sh
git clone --recurse-submodules https://github.com/LILYGO-UI/lilygo-ui-xiaozhi.git
cd lilygo-ui-xiaozhi
```

已有仓库可使用以下命令初始化锁定版本的依赖：

```sh
git submodule update --init --recursive
```

项目直接使用 `third_party/cm0-appkit`，模拟器无需单独安装 AppKit。
两种预设均使用其中附带的 LVGL 9.5.0 源码归档，并禁用 LVGL 网络下载。

### 主机依赖

需要 CMake 3.21 或更高版本（包含 CTest 和 CPack）、供 LPM 使用的 Python 3.11
或更高版本，以及支持 C++17 的 C/C++ 编译器。预设使用 Unix Makefiles。
SDL2 提供显示和音频，FreeType 渲染字体，OpenSSL 处理 TLS，Opus 编解码语音。
AppKit 还需要 `patch` 来应用 LVGL 补丁。

#### Debian 12 / Ubuntu 24.04 或更高版本

```sh
sudo apt update
sudo apt install build-essential cmake git pkg-config patch libsdl2-dev \
  libfreetype6-dev libssl-dev libopus-dev ca-certificates curl \
  python3 python3-venv python3-pip
```

较旧发行版需要先将 CMake 和 Python 升级到上述最低版本。
模拟器使用 SDL2 音频；ALSA 开发文件需在设备 SDK 中提供。

#### macOS

按需安装 Xcode Command Line Tools，并通过 [Homebrew](https://brew.sh/) 安装依赖。
完成 Homebrew 的 Shell 配置，确保 `brew` 及其安装的工具位于 `PATH` 中：

```sh
xcode-select --install
brew install git cmake pkg-config patch sdl2 freetype openssl@3 opus curl python
```

#### LPM

CMake 在配置阶段调用 `lpm metadata --format cmake`。
使用 Python 3.11 或更高版本，在项目虚拟环境中安装 LPM：

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install "git+https://github.com/LILYGO-UI/lpm.git"
lpm --version
```

每次打开新终端后，执行 `. .venv/bin/activate` 重新激活环境。
CMake 会同时搜索 `.venv/bin` 和 `PATH`；如需使用其他位置的 LPM，
可在配置时传入 `-DLPM_EXECUTABLE=/absolute/path/to/lpm`。

构建前检查主机环境：

```sh
cmake --version
python --version
pkg-config --modversion sdl2 freetype2 openssl opus
lpm metadata --format cmake
```

### 设备交叉编译器

仅构建模拟器时可跳过此步骤。设备构建需要在开发机上单独安装 AArch64 Linux
交叉编译器。

**Debian / Ubuntu：**

```sh
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

**Apple Silicon macOS：**

```sh
brew install aarch64-unknown-linux-gnu dpkg
```

SDK 准备脚本使用 `dpkg-deb`，Debian / Ubuntu 通常由 `dpkg` 提供该命令。
BSP 要求 `PATH` 中存在以下名称的编译器：

```sh
aarch64-linux-gnu-gcc --version
aarch64-linux-gnu-g++ --version
```

支持的主机环境与编译器要求见 [CM0BspBuilder](https://github.com/LILYGO-UI/CM0BspBuilder)。
BSP 提供目标平台的头文件和库，不包含开发机使用的交叉编译器。

## 构建与测试

完成环境搭建后，在仓库根目录使用 LPM。它从 `lpm.toml` 读取构建预设和打包输出目录。

### 桌面模拟器

```sh
lpm start --foreground
```

此命令会配置、构建并以前台模式运行模拟器。可使用 `lpm start --no-build --foreground`
复用已有构建。Debug 构建启用测试，产物位于 `build/host-simulator`。
运行需要图形桌面会话；真实语音功能还需要麦克风权限、音频设备、正常的 DNS、
网络连接和 CA 证书。

正常启动会执行真实绑定并连接服务。模拟器将设备身份保存到项目的
`.xiaozhi/device.conf`，连接配置保存到 `.xiaozhi/connection.conf`，
不受启动时工作目录影响。`.xiaozhi/` 已加入 Git 忽略规则，目录权限为 `0700`，
配置文件权限为 `0600`。不要公开其中的内容。

如需离线预览界面，显式开启模拟器的模拟会话：

```sh
LILYGO_XIAOZHI_MOCK=1 lpm start --foreground
```

此模式提供模拟的对话事件，不具备离线语音助手功能。
可通过以下命令在本地检查麦克风、Opus 和回放，不连接服务端：

```sh
build/host-simulator/lilygo-ui-xiaozhi-helper audio-loopback --seconds 1
```

### 测试

配置、构建主机模拟器并运行完整测试集：

```sh
lpm test
```

| 测试 | 覆盖内容 |
| --- | --- |
| `xiaozhi-core` | 不依赖 LVGL 的会话状态、字幕规则和绑定码校验 |
| `xiaozhi-view-model` | 用户命令、Subject、按住说话和连续对话 |
| `xiaozhi-session` | helper 生命周期、激活、持久化、重启恢复和事件解析 |
| `xiaozhi-audio-capture` | SDL 分段读取、超时、取消及播放排空 |
| `xiaozhi-helper-identity` | 稳定的设备身份和有效的 Client ID 格式 |
| `xiaozhi-protocol-session` | 真实 helper 与本地 WebSocket 服务、模拟音频的交互 |
| `xiaozhi-render-{portrait,landscape,embedded}` | 对话主界面的布局和交互 |
| `xiaozhi-render-history-{portrait,landscape,embedded}` | 聊天记录的布局和交互 |
| `xiaozhi-render-binding-{portrait,landscape,embedded}` | 绑定码布局和激活状态 |

花括号分组各代表三项测试，共 15 项。测试使用模拟 helper 或音频，以及本地
WebSocket 服务，无需服务令牌、外网连接或麦克风权限。

渲染测试将 `xiaozhi-*.ppm` 写入 `build/host-simulator`，覆盖 `568 x 1232`、
`1232 x 568` 及嵌入式父容器。修改 UI 后应检查这些产物；断言通过不能验证视觉布局。
详见 [UI 开发规范](docs/01-user-interface.md)。

### CM0 设备构建

设备预设面向 AArch64 架构的 CM0 Raspberry Pi OS，使用 Release 模式，
禁用主机测试，并安装到 `/usr` 下。

CM0 BSP `0.1.0` 包含 ALSA 开发文件和 OpenSSL 运行库，但构建协议 helper
前还需要补齐 OpenSSL 开发文件和 Opus。先准备 SDK，再构建和打包：

```sh
python3 scripts/prepare-helper-sdk.py
lpm pack
```

[准备脚本](scripts/prepare-helper-sdk.py)会按需通过
[工具链包装脚本](cmake/cm0-bsp-toolchain.cmake)下载锁定版本的 BSP，
然后下载固定版本的 Debian ARM64 软件包。两个阶段均校验 SHA-256。
目标文件解压到 `.cache/cm0-bsp/0.1.0/sysroot`，附加软件包缓存在
`.cache/helper-sdk-packages`；不会向开发机安装 Linux 库。
首次运行需要访问 GitHub Releases 和 `deb.debian.org`，并需要 Python 3、CMake、
`curl` 和 `dpkg-deb`。

后续运行会复用缓存。清除 SDK 缓存后需要重新执行准备步骤。
脚本目前固定使用项目内的 sysroot 路径；在 CMake 中修改 `CM0_BSP_CACHE_DIR`
不会改变脚本的目标目录。无需在相邻目录克隆 BSP，主机库也不能替代缺失的目标平台依赖。

`cm0-cross` 预设要求 `LILYGO_XIAOZHI_BUNDLED_HELPER=ON`，缺少 helper 依赖时会失败。
仅当目标平台另行提供兼容 helper 时才应使用 `AUTO` 或 `OFF`；
CM0 不自带旧版 K230 helper。运行时覆盖方式见
[helper 选择说明](docs/03-runtime-configuration.md)。

CPack 将 ARM64 `.deb` 输出到 `dist/`。软件包包含应用、
`/usr/libexec/lilygo-ui-xiaozhi-helper`、启动器条目及图标、桌面条目、
AppStream 元数据和字体许可声明。设备依赖包括 `lilygo-ui-appkit-dev >= 0.1.0`、
`libssl3`、`libasound2`、`libopus0` 和 `ca-certificates`。
AppKit 提供共享运行时字体。

将生成的软件包传到 CM0 设备后，使用 APT 安装以补齐依赖。
版本变化时，请使用实际的软件包文件名：

```sh
sudo apt install --reinstall ./lilygo-ui-xiaozhi_0.1.0_arm64.deb
/usr/libexec/lilygo-ui-xiaozhi-helper --help
```

<details>
<summary>底层 CMake 命令</summary>

LPM 封装了以下步骤。直接使用 CMake 配置时，读取项目元数据仍需要 LPM。

主机构建与测试：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

设备构建与打包：

```sh
python3 scripts/prepare-helper-sdk.py
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

</details>

### 常见问题

| 现象 | 处理方法 |
| --- | --- |
| `lpm is required to read project metadata` | 在 `.venv` 中安装 LPM，将其加入 `PATH`，或设置 `LPM_EXECUTABLE`。 |
| 找不到 `LilyGoUI` 或附带的 LVGL | 执行 `git submodule update --init --recursive`。 |
| 缺少 SDL2、FreeType、OpenSSL 或 Opus | 安装主机开发依赖；设备构建需先准备目标 SDK。 |
| 找不到 Homebrew 库 | 使用 `cmake --preset host-simulator -DCMAKE_PREFIX_PATH="$(brew --prefix)" -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"` 配置。 |
| `Bundled Xiaozhi helper requested but dependencies are missing` | 执行 `python3 scripts/prepare-helper-sdk.py` 后重新配置，并检查是否设置了自定义 BSP 缓存路径。 |
| BSP 或依赖下载失败 | 检查 GitHub Releases / Debian 网络连接、磁盘空间及 `curl` / `dpkg-deb` 是否可用，再重新执行准备步骤。 |
| 设备提示缺少 helper | 通过 `dpkg -L lilygo-ui-xiaozhi` 和 `ldd /usr/libexec/lilygo-ui-xiaozhi-helper` 检查，使用 APT 安装完整软件包后重新打开应用或重连。 |
| 绑定、录音或播放失败 | 检查网络连接、麦克风权限和音频设备选择，详见[运行排障](docs/03-runtime-configuration.md)。 |

## 文档

详细指南目前使用中文编写。

| 指南 | 内容 |
| --- | --- |
| [架构总览](docs/00-overview.md) | 模块边界、事件、进程生命周期和恢复机制 |
| [UI 开发规范](docs/01-user-interface.md) | 字体、布局、所有权和 UI 验证 |
| [Linux 参考与 CM0 适配](docs/02-linux-reference.md) | 上游交互参考、屏幕尺寸和设备音频 |
| [运行配置](docs/03-runtime-configuration.md) | 绑定、协议 helper、环境变量、身份、存储和音频排障 |

## 致谢

协议实现参考 T-Display K230 `k230_launcher` 的小智实现，
基线提交为 `bb831ab358b66f5bd9a87ecd7c580fee4537492e`。
交互和状态流参考
[100askTeam/xiaozhi-linux](https://github.com/100askTeam/xiaozhi-linux/tree/9eff7f419122d2ba0a7ec70bd669edfc72dd9066)。
应用保留自身的 AppKit 与 MVVM 架构，界面使用 LILYGO UI AppKit 和
[LVGL](https://lvgl.io/)，通过 [xiaozhi.me](https://xiaozhi.me) 完成绑定。
参考范围详见[适配说明](docs/02-linux-reference.md)。
