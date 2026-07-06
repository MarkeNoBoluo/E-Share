# E-Share 软件设计方案

## 1. 目标

E-Share 是一个局域网内音视频共享应用。每台设备运行同一个应用，应用同时具备发送和接收能力。

核心场景：

- 设备加入同一局域网后，可以自动发现彼此。
- 用户在应用中选择一个在线设备。
- 当前设备采集本机屏幕和音频，并将音视频流发送给目标设备。
- 目标设备收到控制指令后自动拉流播放。
- 任意设备都可以作为发送端，也可以作为接收端。

本设计优先复用现有两个项目：

- `D:\Repositories\QT-Resources\RTSP-Pusher`：负责屏幕/音频采集、H.264/AAC 编码、RTSP 推流。
- `D:\Repositories\QT-Resources\RTSP-Player`：负责 RTSP 拉流、音视频解码、SDL 播放，并支持 `--winid` 嵌入外部窗口。

## 2. 总体架构

虽然产品形态是一个应用，但内部应拆成控制面和媒体面。

```text
E-Share App
  UI Layer
    设备列表
    推流控制
    接收播放区域
    状态与日志

  Control Plane
    设备发现
    在线状态
    控制服务
    会话管理

  Media Plane
    RTSP Server
    PushService
    PullService
```

### 2.1 控制面

控制面负责设备之间的低频信令，不承载音视频数据。

职责：

- 局域网设备发现。
- 在线心跳。
- 会话创建和关闭。
- 通知目标设备开始播放或停止播放。
- 上报设备状态，例如空闲、正在发送、正在接收。

### 2.2 媒体面

媒体面负责音视频采集、编码、传输、解码和播放。

职责：

- 本机作为发送方时，启动 RTSP Server 和 PushService。
- 本机作为接收方时，启动 PullService 拉流播放。
- 处理推流断线、拉流断线、停止播放、资源释放。

## 3. 核心传输模型

推荐采用“发送方推到本机 RTSP Server，接收方拉流”的模型。

```text
A 设备选择 B 设备

A 启动本机 RTSP Server
A 启动 PushService
A 推流到 rtsp://A_IP:8554/live/session_id
A 通过控制信令通知 B 播放该 URL
B 启动 PullService
B 从 A 拉流播放
```

反向投送时流程相同，只是 A 和 B 的角色互换。

不建议第一版做“发送端直接推到接收端”。当前 `RTSP-Pusher` 的模型是推到 RTSP 服务地址，`RTSP-Player` 的模型是从 RTSP URL 拉流。采用发送方本机 RTSP Server 可以最大限度复用现有项目，减少对媒体管线的改动。

## 4. 模块设计

### 4.1 MainWindow

主界面模块。

职责：

- 展示在线设备列表。
- 展示本机状态。
- 提供“推送到设备”“停止推送”“停止接收”等操作。
- 嵌入接收播放窗口。
- 展示当前会话 URL、码率、帧率、错误状态。

第一版可以用 Qt 实现主界面，播放器通过 `RTSP-Player.exe --winid <HWND>` 嵌入到 Qt 控件。

### 4.2 DeviceDiscovery

设备发现模块。

第一版建议使用 UDP broadcast。

发现流程：

```text
发送端广播 ESHARE_DISCOVER
在线设备回复 ESHARE_HERE
发送端维护在线设备表
超时未收到心跳则标记离线
```

设备信息：

```json
{
  "protocol": "eshare",
  "version": 1,
  "deviceId": "uuid",
  "deviceName": "Office-PC",
  "ip": "192.168.1.25",
  "controlPort": 17680,
  "status": "idle"
}
```

### 4.3 ControlServer

每个应用实例都启动一个本地控制服务，供其他设备调用。

建议第一版使用 HTTP，后续需要实时状态时再增加 WebSocket。

接口：

```text
GET  /status
POST /session/play
POST /session/stop
POST /session/ping
```

播放请求：

```json
{
  "sessionId": "20260706-001",
  "sourceDeviceId": "device-a",
  "url": "rtsp://192.168.1.20:8554/live/20260706-001",
  "fullscreen": false,
  "audio": true
}
```

状态响应：

```json
{
  "deviceId": "device-b",
  "deviceName": "MeetingRoom-PC",
  "status": "receiving",
  "activeSessionId": "20260706-001"
}
```

### 4.4 SessionManager

会话管理模块。

职责：

- 创建推送会话。
- 生成 RTSP URL。
- 启停本机 PushService。
- 向目标设备发送播放指令。
- 停止会话时同时停止本机推流和目标设备播放。
- 处理异常状态，例如目标设备离线、推流失败、拉流失败。

会话状态：

```text
Idle
Preparing
Pushing
Receiving
Stopping
Error
```

### 4.5 RtspServerManager

RTSP 服务管理模块。

第一版建议集成或随应用打包 MediaMTX。

职责：

- 检查 RTSP Server 是否已启动。
- 按需启动 RTSP Server。
- 分配推流路径，例如 `/live/<sessionId>`。
- 停止应用时释放服务进程。

默认地址：

```text
rtsp://<local-ip>:8554/live/<sessionId>
```

### 4.6 PushService

推流服务模块。

第一版可以通过 `QProcess` 启动现有 `RTSP-Pusher.exe`。

示例：

```text
RTSP-Pusher.exe
  --url rtsp://192.168.1.20:8554/live/20260706-001
  --screen 0
  --output-size 1920x1080
  --fps 30
  --bitrate 8000
  --transport tcp
```

第二版再将 `RTSP-Pusher` 的核心类源码集成进主应用，形成可直接调用的 `PushService::start(config)` 和 `PushService::stop()`。

### 4.7 PullService

拉流播放服务模块。

第一版可以通过 `QProcess` 启动现有 `RTSP-Player.exe`。

示例：

```text
RTSP-Player.exe
  --url rtsp://192.168.1.20:8554/live/20260706-001
  --transport tcp
  --setpts-zero
  --winid 0x123456
```

第二版再将播放器核心类集成到主应用中，形成可直接调用的 `PullService::start(url, windowHandle)` 和 `PullService::stop()`。

## 5. 典型流程

### 5.1 应用启动

```text
启动 MainWindow
加载本机配置
生成或读取 deviceId
启动 ControlServer
启动 DeviceDiscovery
刷新在线设备列表
进入 Idle 状态
```

### 5.2 推送到目标设备

```text
用户选择目标设备
SessionManager 创建 sessionId
RtspServerManager 启动本机 RTSP Server
PushService 开始推流到本机 RTSP Server
SessionManager 调用目标设备 /session/play
目标设备 PullService 开始拉流播放
双方状态更新为 Pushing / Receiving
```

### 5.3 停止推送

```text
用户点击停止
发送方向目标设备调用 /session/stop
目标设备停止 PullService
发送端停止 PushService
必要时停止 RTSP Server
双方回到 Idle
```

### 5.4 接收端被动播放

```text
ControlServer 收到 /session/play
校验当前状态是否允许播放
停止旧 PullService
启动 PullService 播放新 URL
更新状态为 Receiving
```

## 6. 进程级集成方案

第一版优先采用进程级集成，降低风险。

```text
E-Share.exe
  ├─ mediamtx.exe
  ├─ RTSP-Pusher.exe
  └─ RTSP-Player.exe
```

优点：

- 最大程度复用现有两个项目。
- 不需要马上改动 FFmpeg/SDL 线程模型。
- 出问题时边界清晰，便于看日志和单独调试。
- 可以快速验证局域网投送完整链路。

限制：

- 进程间状态同步较弱。
- UI 和媒体管线不是完全一体化。
- 日志、错误码、统计数据需要通过文件或 stdout 汇总。

## 7. 源码级集成方案

第二版再做源码级集成。

目标结构：

```text
src/
  app/
    MainWindow
    DeviceListModel
    SessionManager

  control/
    DeviceDiscovery
    ControlServer
    ControlClient

  media/
    RtspServerManager
    PushService
    PullService

  third_party/
    ffmpeg
    sdl2
```

需要从参考项目中拆出的内容：

- 从 `RTSP-Pusher` 拆出 `RTSPusher`、`PusherConfig`、编码线程、采集线程、mux 线程。
- 从 `RTSP-Player` 拆出 `RTSPlayer`、`SDLRenderer`、解码线程、音频播放、生命周期管理。
- 保留现有状态机、重连机制、统计指标。
- 将 `main.cpp` 中的 CLI 解析和 SDL 主循环职责迁移到主应用服务层。

## 8. 网络与端口

建议默认端口：

```text
UDP 17679   设备发现
TCP 17680   控制服务
TCP 8554    RTSP 服务
```

局域网内需要允许防火墙通过这些端口。第一版可以在启动时检测端口占用，并在 UI 中提示。

## 9. 配置项

建议配置文件：

```json
{
  "deviceName": "Office-PC",
  "controlPort": 17680,
  "discoveryPort": 17679,
  "rtspPort": 8554,
  "defaultOutputSize": "1920x1080",
  "defaultFps": 30,
  "defaultBitrateKbps": 8000,
  "rtspTransport": "tcp",
  "autoAcceptPlayRequest": true
}
```

## 10. 错误处理

关键错误场景：

- 目标设备离线。
- 目标设备拒绝播放。
- RTSP Server 启动失败。
- PushService 推流失败。
- PullService 拉流失败。
- 防火墙阻断控制端口或 RTSP 端口。
- 发送端 IP 选择错误，例如多网卡环境下生成了不可达地址。

处理原则：

- 控制面错误必须反馈到 UI。
- 媒体面错误必须进入会话 Error 状态。
- 停止会话必须尽量清理双方资源。
- 第一版只处理真实会发生的错误，不提前设计复杂容灾。

## 11. 第一版范围

第一版目标是跑通单发送端到单接收端的局域网投送。

包含：

- Qt 主界面。
- UDP 设备发现。
- HTTP 控制服务。
- 在线设备列表。
- 选择设备并推送。
- 接收端自动播放。
- 停止推送。
- 进程级集成 `RTSP-Pusher.exe` 和 `RTSP-Player.exe`。
- 打包或外置 MediaMTX。

不包含：

- 多目标同时投送。
- 权限配对码。
- 文件投送。
- 跨网段发现。
- NAT 穿透。
- 自研 RTSP Server。
- 深度源码级集成。

## 12. 后续演进

第二阶段：

- 加入配对确认和访问控制。
- 支持多设备同时接收。
- 增加设备备注、收藏、历史设备。
- 汇总推流和拉流统计数据。

第三阶段：

- 源码级集成 PushService 和 PullService。
- 将播放画面作为主应用原生控件。
- 优化低延迟参数。
- 支持按网络质量自动调整码率和分辨率。

第四阶段：

- 支持跨网段设备发现。
- 支持中继服务。
- 支持移动端或嵌入式接收端。

## 13. 推荐实施顺序

1. 搭建 Qt 主应用骨架。
2. 实现 DeviceDiscovery 和 ControlServer。
3. 集成 MediaMTX 启停。
4. 用 QProcess 集成 RTSP-Pusher。
5. 用 QProcess 集成 RTSP-Player，并验证 `--winid` 嵌入。
6. 打通 A 设备推送到 B 设备的完整流程。
7. 增加停止、异常、状态显示。
8. 再评估是否进入源码级集成。

