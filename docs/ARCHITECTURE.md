# car-ai-s3 项目架构与技术说明

> 项目：车载 AI 行车状态监测与语音交互终端
> 代码基线：xiaozhi-esp32 1.9.4 / ESP-IDF 5.5.3 / LVGL 9.3.0
> 硬件：GRW ESP32-S3 开发板（ESP32-S3-WROOM-1-N16R8）
> 文档日期：2026-10-22
>
> 本文描述的是**当前仓库实际运行代码**，不是早期规划稿。环境数据通道当前由演示源驱动，
> 真实传感器接入点、尚未实现的告警规则等边界会在文中明确标注。

---

## 1. 项目定位与功能闭环

### 1.1 一句话定位

在 ESP32-S3 上把「车内环境监测 + 摄像头人员检测 + 本地语音助手 + 云端远程查看」
做成一台可离线运行、可联网上报、可语音查询的轻量车载终端。

### 1.2 四条主链路

| 链路 | 输入 | 处理 | 输出 |
| --- | --- | --- | --- |
| 环境监测 | DHT11 接口 / 演示数据源 | `CarMonitor` 100 ms 周期采样、有效性判定 | LVGL 数据页、MCP 查询、MQTT `env` |
| 人员遗留 | GC0308 DVP 摄像头 | esp-dl `pedestrian_detect` 推理、告警状态机 | 摄像头页检测框、数据页告警、MQTT `alarm` |
| 语音交互 | ES7210 麦克风 + AFE WakeNet9 | 唤醒、VAD、Opus 编解码、WebSocket 会话 | ES8311 扬声器、屏幕对话状态、MCP 工具调用 |
| 云端监控 | `CarSnapshot` 一致快照 | JSON 序列化、主题发布、断线补发 | 巴法云 `env/state/alarm/online` |

### 1.3 技术底座与移植边界

| 部分 | 来源 | 本项目改动 |
| --- | --- | --- |
| 小智 AI 固件 | 虾哥 `xiaozhi-esp32` 1.9.4 | 板级适配、车载业务、FreeRTOS 调度与内存治理 |
| LVGL 界面 | LVGL 9.3.0 + `esp_lvgl_port` | 三屏架构、车载数据页、摄像头页、禁滚动布局 |
| 摄像头识别 | 乐鑫 `esp-dl` + `pedestrian_detect`（PICO_S8_V1） | 模型分区、帧缓冲互斥、检测结果入业务快照 |
| 语音前端 | 乐鑫 ESP-SR（AFE + WakeNet9） | 任务栈兜底、优先级与内存策略调整 |
| 摄像头驱动 | 乐鑫 `esp32-camera` | DVP 配置、RGB565 QVGA、预览/推理/拍照共用帧缓冲治理 |

### 1.4 必须说明的能力边界

- **环境数据**：`CarMonitor::Init(true)` 走演示模式，温湿度/光照由 `VirtualSensors` 生成；
  `Dht11Sensor` 当前也是虚拟数据源。真实 DHT11/光照传感器的接入点已经留在
  `CarMonitor::UpdateEnvironmentData()` 的 `else` 分支，替换时不影响 UI、MCP、云端和告警框架。
- **告警规则**：当前完整落地的是「停车后人员遗留」；温度/湿度告警函数是占位实现，
  阈值与滞回参数已定义但未启用。
- **屏幕按钮**：摄像头页原有的三颗触摸按钮因早期触摸不稳定已移除，回调接口仍保留；
  当前主要交互是 SW3 切屏、BOOT 键会话控制、语音与 MCP。
- **云端**：巴法云只做监测数据上报，不承担语音链路；语音仍走小智 WebSocket 服务。

---

## 2. 硬件基线

### 2.1 主控与存储

| 项目 | 型号 / 参数 |
| --- | --- |
| SoC | ESP32-S3-WROOM-1-N16R8，双核 Xtensa LX7，最高 240 MHz |
| 内部 SRAM | 512 KB |
| 外部 PSRAM | 8 MB Octal，80 MHz |
| Flash | 16 MB，QIO 模式 |
| 无线 | 2.4 GHz Wi-Fi（板载，无 4G 模组） |

### 2.2 外设型号与接口

| 外设 | 具体型号 | 接口 / 关键参数 | 引脚 |
| --- | --- | --- | --- |
| 显示屏 | ST7796S | SPI3，480×320，RGB565，80 MHz | MOSI=40，SCLK=41，DC=39，BL=42 |
| 触摸 IC | FT6336U（FT6x36 驱动） | I2C1，地址 0x38，400 kHz，中断驱动 | SDA=1，SCL=2，INT=10 |
| 摄像头 | GC0308（DVP） | QVGA 320×240，RGB565，XCLK 24 MHz | D0~D7=16/18/8/17/15/6/4/9，VSYNC=3，HREF=46，PCLK=7，XCLK=5 |
| 音频 DAC | ES8311 | I2S，24 kHz，地址 0x18 | MCLK=38，BCLK=14，WS=13，DOUT=45 |
| 音频 ADC | ES7210 | I2S，4 路麦克风可选，地址 0x82（8 位） | DIN=12 |
| IO 扩展 | PCA9557 | I2C1，地址 0x19 | P0 屏背光/使能，P1 功放使能，P2 摄像头电源 |
| 姿态传感器 | QMI8658A | I2C1，地址 0x6A，25 Hz 采样 | SDA=1，SCL=2 |
| 环境传感器 | DHT11（接口预留） | 单总线（当前未接实物，使用演示源） | — |
| 按键 | BOOT / SW3 | GPIO0 / GPIO11 | BOOT=会话控制，SW3=三页循环 |

### 2.3 共享 I2C1 拓扑

```
ESP32-S3 I2C1 (SDA=GPIO1, SCL=GPIO2, 400 kHz)
├── PCA9557        0x19   IO 扩展：背光/功放/摄像头电源
├── ES8311         0x18   音频 DAC
├── ES7210         0x82   音频 ADC（8 位地址）
├── FT6336U        0x38   触摸
├── QMI8658A       0x6A   姿态传感器
└── GC0308 SCCB    —      摄像头寄存器配置（复用 I2C1）
```

这条总线是全板最容易被低估的资源：音频、触摸、IMU、摄像头寄存器、电源使能全部挂在上面。
代码中的约束是：

1. 只创建一次总线实例，后续设备通过 `i2c_master_bus_add_device()` 挂接；
2. 每个设备独立配置 `scl_speed_hz` 与事务超时；
3. 初始化探测失败只降级，不死循环等待；
4. 任何 I2C 读失败都不阻塞业务任务，快照里标记 `*_fault`。

---

## 3. 软件分层

```
┌──────────────────────────────────────────────────────────────┐
│ 应用层 Application / MainEventLoop / DeviceState             │
│  - 设备状态机、会话调度、Schedule() 跨任务回调                 │
│  - ScreenManager：聊天页 / 数据页 / 摄像头页                  │
│  - MyInfo、MCP Server、OTA、Settings                          │
├──────────────────────────────────────────────────────────────┤
│ 业务层 CarMonitor / CarMonitorHelper                          │
│  - CarSnapshot 一致快照（互斥锁）                             │
│  - 车辆状态、人员状态、告警状态机、命令队列                    │
│  - 环境数据通道（演示源 / 真实传感器接入点）                   │
├──────────────────────────────────────────────────────────────┤
│ 服务层                                                        │
│  - CarUIHelper：数据页 500 ms 刷新、摄像头页 10 FPS 预览      │
│  - CarPersonDetect：esp-dl 推理、检测框出口                   │
│  - CarCloud：巴法云 MQTT 上报、告警补发                       │
│  - AudioService：AFE、Opus、会话音频队列                      │
│  - Camera / Display / Backlight                               │
├──────────────────────────────────────────────────────────────┤
│ 驱动与板级层 GRW Board                                        │
│  - I2C1 / SPI3 / I2S / DVP / GPIO / PCA9557                   │
│  - ST7796S、FT6336U、GC0308、ES8311、ES7210、QMI8658A         │
└──────────────────────────────────────────────────────────────┘
```

### 3.1 关键文件职责

| 文件 | 职责 |
| --- | --- |
| `main/application.cc` | 启动序列、主事件循环、设备状态机、WebSocket 回调、静默兜底 |
| `main/application.h` | `Application` 单例、`Schedule()`、状态与事件位定义 |
| `components/car_monitor/car_monitor.cc` | 100 ms 监测任务、快照、命令队列、人员遗留告警 |
| `components/car_monitor/car_snapshot.h` | 全系统一致状态结构（环境/人员/告警/故障/网络） |
| `main/car_monitor_helper.cc` | 快照查询封装、MCP `self.car.*` 工具、人员结果入口 |
| `main/car_ui_helper.cc` | 数据页刷新任务、摄像头预览任务、检测框回调 |
| `main/car_person_detect.cc` | 唯一持有 `PedestrianDetect` 的任务，500/1000 ms 推理 |
| `main/car_cloud.cc` | 巴法云 MQTT 任务、JSON 负载、断线补发 |
| `main/screen_manager.cc` | 三个独立 LVGL 屏幕的创建与切换 |
| `main/boards/grw-esp32s3-board/` | 板级引脚、I2C/SPI/LCD/触摸/摄像头/音频初始化 |
| `main/boards/common/esp32_camera.cc` | 帧缓冲互斥、预览/推理取帧、拍照上传与归还 |
| `main/audio/audio_service.cc` | 音频输入/输出/Opus 任务与队列 |
| `main/audio/wake_words/afe_wake_word.cc` | AFE WakeNet9 检测任务与 PSRAM 栈兜底 |
| `main/audio/processors/afe_audio_processor.cc` | AFE 处理、VAD、音频通信任务 |
| `main/display/lcd_display.cc` | LVGL 端口、15 行双缓冲、2 MB 图片缓存 |
| `main/qmi8658.cc` | QMI8658A 驱动、25 Hz 运动检测任务 |

---

## 4. 启动时序：一次「内存仲裁」而非简单初始化

`Application::Start()` 的顺序不是随意的，它同时决定了**内部 RAM 的峰值**和**外设依赖关系**。

```
Board 构造
  ├─ I2C1 总线（1/2）
  ├─ SPI3 总线（40/41/39/42）
  ├─ ST7796S + LVGL 端口
  ├─ FT6336U（INT=10）
  ├─ BOOT / SW3 按键
  └─ GC0308（PCA9557 P2 上电）
        ↓
Application::Start()
  ├─ 1. 音频服务 Initialize + Start（audio_input/output/opus/AFE）
  ├─ 2. DHT11 演示源 Initialize
  ├─ 3. MyInfo Initialize（语音播报信息）
  ├─ 4. ScreenManager Initialize（聊天/数据/摄像头三屏）
  ├─ 5. CarMonitor Init + StartMonitorTask（4 KB / 优先级 5）
  ├─ 6. CarUIHelper Init + 数据页刷新任务 + 摄像头预览任务
  ├─ 7. MainEventLoop 任务（8 KB / 优先级 3）+ 1 s 时钟定时器
  ├─ 8. StartNetwork()（Wi-Fi 就绪）
  ├─ 9. CarPersonDetect::Start()（必须在 Wi-Fi 之后）
  ├─ 10. Qmi8658::StartMotionTask()（25 Hz）
  ├─ 11. 巴法云启动定时器（延迟 15 s，失败自动重试）
  ├─ 12. OTA 版本检查
  ├─ 13. MCP 通用工具 + self.car.* 工具注册
  └─ 14. WebSocket 协议启动 → Idle
```

### 4.1 为什么人员检测必须放在 `StartNetwork()` 之后

esp-dl 加载模型时会瞬时占用内部 RAM。若抢在 `esp_wifi_init()` 之前，Wi-Fi 可能拿不到
RX buffer，返回 `ESP_ERR_NO_MEM` 并 abort；而软复位不会给触摸 IC 断电，重启后触摸可能
读回全 `0x00` 失效。因此顺序是：**先让网络把内部 RAM 的一次性分配做完，再加载模型**。

### 4.2 为什么巴法云要延迟 15 秒

启动窗口里有三个「内部 RAM 大户」：

1. `PedestrianDetect` 模型加载（瞬时占用约 21 KB，最低水位仅约 4 KB）；
2. OTA 版本检查（TLS 握手需要约 20 KB，其中约 16 KB 必须是连续块）；
3. MQTT 客户端初始化（任务栈 + 连接缓冲，数 KB）。

三者叠加会导致 OTA 检查大多数失败、AFE 检测任务创建偶发失败。把 `CarCloud::Start()`
移到 15 s 后的一次性 `esp_timer`，失败后每 15 s 重试，启动窗口就不再被 MQTT 挤占。
代价是云上报晚 15 s 上线，这个代价远小于 OTA/语音初始化失败。

### 4.3 为什么触摸初始化失败必须直接返回

FT6336U 若进入 Monitor 模式，I2C 串口整体关闭，读回全 `0x00`。旧代码在这种情况下
仍把未初始化的句柄交给 `lvgl_port_add_touch()`，触发 `LoadProhibited` → 重启 → 循环。
现在 `InitializeTouch()` 的做法是：

```cpp
esp_lcd_touch_handle_t tp = nullptr;
...
if (tp_ret != ESP_OK || tp == nullptr) {
    ESP_LOGE(TAG, "Touch init failed ... skip touch registration");
    return;   // 优雅降级，不把空/野句柄交给 LVGL
}
```

同时在驱动初始化里写入 `0x86 = 0x00`，禁止 FT6336U 无触摸 30 s 后自动进入 Monitor。
这是「防重启循环」的第一道结构防线。

---

## 5. 运行逻辑

### 5.1 设备状态机

`DeviceState` 定义在 `main/device_state.h`：

| 状态 | 含义 | 主要迁移 |
| --- | --- | --- |
| `Unknown` | 未初始化 | 启动 |
| `Starting` | 启动/配网阶段 | Wi-Fi 就绪 |
| `WifiConfiguring` | 配网模式 | 配网完成 |
| `Idle` | 空闲，唤醒词检测开启 | 唤醒词 / BOOT 键 → `Connecting` |
| `Connecting` | 建立/等待 WebSocket 会话 | 服务端 hello → `Listening` |
| `Listening` | 上行麦克风音频 | VAD 结束 / 服务端 tts start → `Speaking` |
| `Speaking` | 下行 TTS 播放 | tts stop → `Listening` 或 `Idle` |
| `Upgrading` | OTA | 完成重启 |
| `Activating` | 设备激活 | 激活完成 |
| `AudioTesting` | 音频自检 | 退出 |
| `FatalError` | 不可恢复错误 | 重启 |

### 5.2 一次语音会话的完整链路

```
用户说"你好小智"
  ↓ AFE WakeNet9（audio_detection 任务，优先级 3）
Application::OnWakeWordDetected()
  ↓ 打开音频通道
WebSocket 连接 + hello
  ↓
Listening：ES7210 → I2S RX → AFE(AEC/NS/VAD) → Opus encode → WebSocket 上行
  ↓ 服务端识别/推理
TTS start → Speaking：WebSocket 下行 Opus → 解码 → ES8311 → 功放
  ↓ TTS stop
Listening（AutoStop / Realtime）或 Idle
```

关键兜底：

| 场景 | 兜底动作 |
| --- | --- |
| AutoStop 模式 Listening 静默 8 s | `StopListening()` → Idle，重新开启唤醒词 |
| Realtime 模式 Listening 静默 30 s | 同上；这是发送链路死亡/服务端沉默时唯一的本地逃生 |
| 服务端下发 `goodbye` | 主循环中 `CloseAudioChannel()`，让服务端立即释放会话 |
| 服务端 10 s 无 hello | 握手失败位 + 错误处理，回 Idle |
| 会话结束 | `OnAudioChannelClosed` → Idle，重开唤醒词 |

`goodbye` 分支是第二次唤醒问题的关键修复：此前设备只是被动等 TCP 断开，服务端残留会话
会拒绝下一次连接。现在在收到 `goodbye` 时主动优雅关闭，服务端会话立即释放。

> 注意：`goodbye` 回调运行在 WebSocket 的 `ssl_receive` 任务里，不能直接销毁 WebSocket；
> 必须通过 `Schedule()` 切到主事件循环再 `CloseAudioChannel()`，否则会 use-after-free 或自等死锁。

### 5.3 三屏交互

`ScreenManager` 管理三个**独立 LVGL screen**，而不是一个页面里切 tab：

| 屏幕 | 内容 | 刷新策略 |
| --- | --- | --- |
| 聊天页 | 小智对话、状态栏、表情 | LVGL 默认 |
| 数据页 | 顶部车辆状态 / 左栏环境 / 右栏人员 / 底部告警 | 500 ms 快照刷新，文本去重 |
| 摄像头页 | 320×240 实时预览 + 检测框叠加 | 仅本页可见时 10 FPS 取帧 |

SW3（GPIO11）单击循环切换：`聊天 → 数据 → 摄像头 → 聊天`。
两页车载界面都执行 `lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE)`，这是性能设计的一部分，
不是 UI 偏好：一旦出现滚动条，脏区会回到全屏，SPI 带宽立刻成为瓶颈。

### 5.4 监测任务循环

`CarMonitor::MonitorTaskFunc()` 每 100 ms 执行：

```
UpdateEnvironmentData()      // 演示源或真实传感器
ProcessCommand()             // 排空命令队列（切车辆状态/确认告警/测试告警）
UpdateAlertStateMachine()    // 人员遗留判定
```

人员遗留规则：

| 参数 | 值 | 含义 |
| --- | --- | --- |
| `PARKING_GRACE_PERIOD_MS` | 15 000 ms | 停车后宽限期 |
| `PERSON_DETECTION_THRESHOLD` | 3 | 连续 3 次检测到有人 |
| `PERSON_DETECTION_VALIDITY_MS` | 2 000 ms | 检测结果有效期 |
| `ALERT_CONFIRM_DURATION_MS` | 30 000 ms | 确认后静音 30 s |

数据有效性由 `valid` + `age_ms` 两个字段共同表达：超过有效期后 UI/云端看到的是
「无效」而不是旧值，避免把过期数据当实时数据。

### 5.5 云端上报循环

`CarCloud::TaskFunc()` 以 1 s 为节拍：

```
链路检查（Wi-Fi / MQTT）
  ├─ 首次连接或长时间断线 → Connect（其余交给 esp-mqtt 自动重连）
  ├─ 连接建立 → 订阅 4 个主题
  ├─ 断线告警缓存补发（最多 32 条）
  ├─ 读 CarSnapshot
  ├─ 告警变化 → /set（≤1 s）
  └─ 每 5 s env+state，每 30 s online
```

---

## 6. FreeRTOS 设计

### 6.1 任务清单

| 任务 | 核 | 优先级 | 栈 | 周期/触发 | 职责 |
| --- | --- | --- | --- | --- | --- |
| `audio_input` | 未显式钉核 | 8 | 6 144 B | I2S DMA | 硬实时音频输入，优先级最高 |
| `car_monitor` | 未显式钉核 | 5 | 4 096 B | 100 ms | 环境采样、命令、告警状态机 |
| `car_ui_update` | 未显式钉核 | 5 | 4 096 B | 500 ms | 数据页快照刷新 |
| `opus_codec` | 未显式钉核 | 4 | 26 624 B | 队列 | Opus 编码/解码消费端 |
| `audio_output` | 未显式钉核 | 4 | 4 096 B | 队列 | TTS 播放 |
| `LVGL` | **CPU1** | 4 | 由 `esp_lvgl_port` 配置 | 33 ms | 渲染与 flush |
| `main_event_loop` | 未显式钉核 | 3 | 8 192 B | 事件组 | 状态机、会话、Schedule 回调 |
| `cam_preview` | **CPU0** | 3 | 4 096 B | 100 ms | 摄像头页取帧、bswap、提交 LVGL |
| `audio_communication` | 未显式钉核 | 3 | 4 096 B | AFE 输出 | AFE 处理结果搬运 |
| `audio_detection` | 未显式钉核 | 3 | 4 096 B | AFE fetch | 唤醒词检测；内部栈失败时回退 PSRAM |
| `car_cloud` | **CPU0** | 2 | 5 120 B | 1 000 ms | 巴法云 MQTT、JSON、补发 |
| `imu_motion` | **CPU0** | 2 | 2 560 B | 40 ms | QMI8658A 采样、运动判决 |
| `encode_wake_word` | 未显式钉核 | 2 | 28 672 B（PSRAM） | 唤醒事件 | 唤醒词音频编码上传 |
| `person_detect` | **CPU0** | 1 | 4 096 B | 500/1 000 ms | esp-dl 推理、检测框 |

> 表中「未显式钉核」的任务由 FreeRTOS 调度器在双核间迁移；LVGL 显式钉 CPU1，
> 摄像头/检测/云/IMU 显式钉 CPU0。这样做的目的是把**实时音频 + 渲染**与
> **图像/网络/推理**分开，减少同核抢占。

### 6.2 优先级设计规则

1. **硬实时最高**：`audio_input` 优先级 8。I2S DMA 不等人，喂数晚了就是爆音/丢帧。
2. **消费者不低于生产者**：`opus_codec` 从 2 提到 4，因为生产端
   `audio_communication` 是 3，而编码队列深度只有 2 且 `push` 会阻塞。
   原设置让生产端抢占消费端，生产端卡在 push → 停止 fetch AFE → 环形缓冲溢出刷屏。
3. **CPU0 上的后台任务从低到高**：`person_detect`(1) < `car_cloud`/`imu_motion`(2) <
   `cam_preview`(3)。推理最重但最可延迟；预览要保 10 FPS；云上报可等。
4. **CPU1 保渲染与音频**：LVGL 显式钉 CPU1（优先级 4），音频输入优先级 8。
   音频任务本身未显式钉核，但它是最高优先级，调度器会优先保证其运行；
   不要把摄像头预览放到 CPU1：逐帧 153 600 字节的拷贝会与 WakeNet9 抢核，
   饿死 CPU1 IDLE → 触发 task watchdog，同时拖垮 AFE 喂数。
   预览钉 CPU0 后该竞争消失。
5. **不要盲目上调 AFE 内部任务**：把 AFE 处理任务从默认值上调到 5 会与 CPU1 的 LVGL
   产生抢占冲突，实测出现屏幕不亮/重启循环，因此保持 `afe_perferred_priority = 1`。
6. **回调不做重活**：WebSocket/检测/音频回调只置事件位或投递队列，耗时的 LVGL、
   关闭通道、状态切换统一 `Schedule()` 到主循环。

### 6.3 同步原语

| 原语 | 用途 | 关键约束 |
| --- | --- | --- |
| `EventGroup` | 主循环事件（SEND_AUDIO、WAKE_WORD、VAD、CLOCK_TICK、ERROR、SCHEDULE） | 只置位，不阻塞 |
| `Queue` | `CarMonitor` 命令队列（10 条）、音频编码/解码/播放队列 | 命令队列 `xQueueSend(..., 0)` 非阻塞 |
| `SemaphoreHandle_t` | `CarSnapshot` 互斥、单例初始化互斥 | 临界区只做 `memcpy`，不调用 I2C/Flash |
| `std::mutex fb_mutex_` | 摄像头 `fb_` 的取帧/归还/编码并发 | 预览、推理、拍照共用 |
| `lvgl_port_lock()` | 所有 LVGL 对象操作 | 超时 20~100 ms；拿不到就丢弃本帧，绝不阻塞业务 |
| `std::atomic` / `bool` | 任务运行标志、连接状态 | 单写单读的状态量 |

### 6.4 看门狗与实时性

- `CONFIG_ESP_TASK_WDT_TIMEOUT_S=10`，同时检查 CPU0/CPU1 IDLE；
- 所有任务循环都有 `vTaskDelay`/`vTaskDelayUntil`/队列阻塞，不存在忙等；
- 预览任务只在摄像头页取帧，离开页面立即停止拷贝；
- 人员检测在小智会话期间从 500 ms 降到 1 000 ms，让出 PSRAM 带宽给音频；
- 云上报 1 s 节拍，实际网络发送 5 s/30 s，不占用 CPU1。

---

## 7. 内存设计（本项目最核心的工程约束）

### 7.1 512 KB 内部 SRAM 的账本

启动日志给出的动态堆约为 **259 KB**（206 + 21 + 32 KB 三块），其中
`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536` 预留 **64 KB** 给 DMA/内部专用分配，
普通内部堆约 **195 KB**。静态区 + IDF/Wi-Fi 固定占用约 **253 KB**。

| 占用项 | 约占用 | 说明 |
| --- | --- | --- |
| 任务栈合计 | ~90 KB | 含系统任务；`opus_codec` 26 KB 最大 |
| LVGL 绘制缓冲 | **28.8 KB** | 480 × 15 × 2 B × 双缓冲，必须在内部 DMA RAM |
| I2S 音频 DMA | ~17 KB | RX 2ch + TX 2ch，6 × 240 帧 |
| 摄像头 DMA 行缓冲 | ~7.7 KB | 帧缓冲本身在 PSRAM |
| esp-dl 张量池 | ~7.7 KB | 模型主体在 PSRAM |
| Wi-Fi/LWIP/LVGL 小对象 | 40–80 KB | 运行时浮动，部分走 64 KB DMA 池 |
| 稳态空闲 | **32–33 KB**（全周期最低 9–17 KB） | 随网络重试/会话波动 |

关键结论：**内部 RAM 的瓶颈不是 512 KB 总量，而是开机后只有约 195 KB 可正常动态分配，
且必须留出连续块给 DMA/TLS。** 所有优化都围绕「减少内部堆峰值 + 避免碎片」展开。

### 7.2 8 MB PSRAM 用在哪里

| 用途 | 大小 | 说明 |
| --- | --- | --- |
| 摄像头帧缓冲 | 2 × 153 600 B ≈ 300 KB | `fb_count=2`，`CAMERA_FB_IN_PSRAM` |
| 摄像头页预览双缓冲 | 2 × 153 600 B ≈ 300 KB | 乒乓写，LVGL 读另一块 |
| 人员检测帧缓冲 | 153 600 B ≈ 150 KB | `GetRawFrame()` 原始 RGB565BE |
| LVGL 图片缓存 | 上限 2 MB | `lv_image_cache_resize(2MB)`，按需分配 |
| esp-dl 模型/张量 | 主体 | 模型 435 KB 从 flash 分区映射，张量池在 PSRAM |
| AFE 环形缓冲/工作区 | 按需 | `AFE_MEMORY_ALLOC_MORE_PSRAM` |
| 唤醒词编码任务栈 | 28 KB | `xTaskCreateStatic` + PSRAM 栈 |
| JPEG 编码块 | 40 × 512 B ≈ 20 KB | 拍照上传时临时分配 |
| mbedTLS 大缓冲 | 按需 | `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`，优先 PSRAM |

PSRAM 不是「随便放」：DMA 描述符、中断上下文数据、频繁访问的小结构、任务 TCB
必须留在内部 RAM；PSRAM 只放**大块、低频、非 DMA 直连**的数据。

### 7.3 16 MB Flash 用在哪里

分区表 `partitions.csv`：

| 分区 | 类型 | 偏移 | 大小 | 用途 |
| --- | --- | --- | --- | --- |
| `nvs` | data/nvs | 0x9000 | 16 KB | Wi-Fi、设备配置、巴法云凭据、boot_id |
| `otadata` | data/ota | 0xD000 | 8 KB | OTA 启动选择 |
| `phy_init` | data/phy | 0xF000 | 4 KB | RF 校准 |
| `model` | data/spiffs | 0x10000 | 960 KB | ESP-SR 模型（`srmodels.bin`，实际 291 KB） |
| `ota_0` | app/ota_0 | 0x100000 | 6 MB | 当前应用固件（实际 5.86 MB） |
| `ota_1` | app/ota_1 | 0x700000 | 6 MB | OTA 备份槽 |
| `pedestrian_det` | data/spiffs | 0xD00000 | 1 MB | esp-dl 人员检测模型（实际 435 KB） |

设计意图：

- **双 OTA 槽**保证升级失败可回滚；
- **模型独立分区**，改应用代码不必每次重烧 435 KB 模型；
- 模型分区留足余量（960 KB / 1 MB），方便替换更大模型；
- 应用固件已占 6 MB 槽的约 93%，后续加功能要优先考虑裁剪字体/资源或调整分区。

### 7.4 分配规则（硬约束）

| 必须内部 RAM | 可以 PSRAM | 绝对不要 PSRAM |
| --- | --- | --- |
| LVGL 绘制缓冲（DMA） | 摄像头帧缓冲 | 任务栈中会调用 NVS/OTA/Flash 写的任务 |
| I2S DMA 缓冲 | 预览双缓冲 | DMA 描述符 |
| 中断/ISR 数据 | esp-dl 张量/模型 | 高频小对象（cache 抖动） |
| 任务 TCB | AFE 工作区 | 音频实时路径的大缓冲 |
| 协议/TLS 控制结构 | JPEG 块、图片缓存 | 看门狗/事件组内部结构 |

> `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y` 只用于「不碰 flash」的低频任务。
> ESP32-S3 在 NVS/OTA 写 flash 时会关闭 cache，PSRAM 栈上的任务若此时运行会崩溃。
> 因此 `car_cloud`（启动时读写 NVS）和音频/主循环任务都留在内部 RAM。

### 7.5 内存优化清单与「保留流畅度」的取舍

| 优化 | 收益 | 代价 | 结论 |
| --- | --- | --- | --- |
| LVGL 绘制缓冲 30 行 → 15 行 | 省 **28.8 KB** 内部 RAM | 每次 flush 行数减半，预览横向细缝数量约翻倍 | 接受；触摸从「多次点击才响应」恢复 |
| 车载界面拆成数据页/摄像头页 | 脏区从 307 KB 降到几 KB | 多两个 screen 对象 | 帧率从 26–30 提升到可用水平 |
| 两页禁用滚动 | 防止脏区回到全屏 | 内容必须固定布局 | 结构性解决卡顿 |
| 数据页 500 ms 刷新 + 文本去重 | 减少无意义重绘 | 数值最多滞后 500 ms | 切页即最新，开销可忽略 |
| 预览只在摄像头页取帧 | 省 CPU/PSRAM 带宽 | 离开页面画面不更新 | 符合交互预期 |
| 预览 10 FPS（100 ms） | 比 30 FPS 省 2/3 拷贝 | 画面略顿 | 车载监控足够 |
| 人员检测会话期 500→1000 ms | 给音频让 PSRAM 带宽 | 检测延迟增加 | 会话期优先保语音 |
| 巴法云延迟 15 s + 9501 非 TLS | 避开启动峰值，省 30–50 KB | 云上线晚 15 s，明文传输 | 监测数据可接受 |
| `car_cloud` 栈 2560→5120 B | 消除栈溢出重启 | 多占 2.5 KB 内部 RAM | 稳定性优先 |
| 2 MB LVGL 图片缓存 | 避免 PNG 反复解码 | 占 PSRAM（按需） | PSRAM 充足，不影响内部堆 |
| `%f` 禁用，整数拼小数 | 规避 nano newlib 失效 | 代码略啰嗦 | 全项目统一 |

**为什么这些优化没有牺牲流畅度**：

- 480×320×2 = **307 200 B/帧**，SPI 80 MHz 纯传输约 **30.7 ms**，全屏滚动实测约 40 ms/帧
  （26–30 FPS），其中传输占约 77%，渲染仅约 9 ms。瓶颈是 SPI 带宽，不是 CPU。
- 拆页 + 禁滚动后，每帧脏区从 307 KB 降到几 KB，传输时间按比例下降，所以「省内存」
  和「提帧率」在这里是同一件事：**减少每帧要传的字节数**。
- 15 行双缓冲仍然保留双缓冲重叠（渲染下一块与传输当前块并行），所以没有退回单缓冲的顿挫；
  代价只是分块更多、细横线更明显，这是无 TE 引脚 SPI 屏的物理限制。
- 预览双缓冲放在 PSRAM，LVGL 会先拷贝到内部 DMA 绘制缓冲再推屏，不存在「SPI 直接从
  PSRAM 取数」的问题（SPI3 不支持外部内存 DMA，会要求等大内部中转）。

### 7.6 栈溢出后的处理方案

**现象**：串口出现 `***ERROR*** A stack overflow in task car_cloud`，随后整机重启；
若任务在启动路径上，表现为拿到 IP 后立刻重启，反复循环。

**根因**：`car_cloud` 原栈 2 560 B，而启动路径包含
`nvs_get/set` + `esp_mqtt_client_init` + TLS/网络回调，深调用链超过栈容量。

**处理步骤**：

1. **先定位任务名**：栈溢出日志会直接给出 task 名，不要先怀疑 heap。
2. **加高水位观测**：在任务稳定运行后打印
   `uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)`；`car_cloud` 在连接成功后
   打印栈余量，作为后续调整依据。
3. **一次性加够**：2 560 → 5 120 B，而不是每次加 512 B 反复试；内部 RAM 只多花 2.5 KB。
4. **区分「栈不够」与「栈太大」**：`opus_codec` 26 KB 是最大栈，但它是音频路径，
   不能为了省内存盲目砍；要用完整会话（唤醒→对话→TTS→Idle）的最低水位判断。
5. **PSRAM 栈只给安全任务**：AFE `audio_detection` 内部栈创建失败时，回退到
   `xTaskCreatePinnedToCoreWithCaps(..., MALLOC_CAP_SPIRAM)`；该任务只做 fetch/存音频，
   不写 flash。`car_cloud` 因启动时读写 NVS，**不能**迁 PSRAM。
6. **失败即降级**：任务创建失败返回 `false`，调用方只告警、不阻塞启动；
   模型/摄像头/云任何一路失败都不影响预览、音频、界面。
7. **防重启循环**：触摸句柄初始化为 `nullptr` 并检查返回值；I2C 探测失败不进入死循环；
   云上报失败由定时器重试而不是整机重启。

**预防策略**：

- 新任务先按「最深调用链 + 1 KB 余量」估算，再用高水位实测回收；
- 不在回调里做深调用（JSON 解析、LVGL、NVS 都切主循环）；
- 任何 `ESP_ERROR_CHECK` 都不放在「从机可能缺席」的路径上；
- 关键任务栈用静态分配或 `xTaskCreateStatic`，避免 heap 碎片导致创建失败。

---

## 8. 外设协同与数据流

### 8.1 显示链路

```
LVGL 对象树
  → 15 行双缓冲（内部 DMA RAM，480×15×2×2 = 28.8 KB）
  → esp_lcd_panel_draw_bitmap
  → SPI3 80 MHz（MOSI40 / SCLK41 / DC39）
  → ST7796S 480×320
```

- `trans_size=0`：本项目的 `esp_lvgl_port` 版本不支持传输累积缓冲，设置无效；
- `buff_dma=1`、`buff_spiram=0`：绘制缓冲必须在内部 DMA RAM；
- 背光由 GPIO42 PWM + PCA9557 P0 协同控制；
- 无 TE 引脚，移动镜头时的分块细横线只能通过缩小预览区/减少脏区缓解。

### 8.2 摄像头链路

```
GC0308 DVP → esp32-camera（2 帧缓冲，PSRAM）
  ├─ GetPreviewFrame() → bswap 大端→小端 → 预览双缓冲 → LVGL image → 屏
  ├─ GetRawFrame()     → 原样 RGB565BE → esp-dl ImagePreprocessor → 推理
  └─ Capture()/Explain() → JPEG 编码线程 → HTTP multipart 上传 → 归还 fb
```

三条路径共用 `fb_`，因此 `fb_mutex_` 是必须的：

- 预览/推理取帧时若 `fb_` 已被拍照保留，则**只读借用**，不调用会阻塞的 `fb_get()`；
- 拍照上传结束（成功、失败、早退）都必须 `release_fb()`，否则预览永远拿不到新帧，
  表现为「拍照后画面卡死」；
- 上传失败路径先排水到 JPEG 终止块再 `join()`，避免队列满时编码线程与主循环互等死锁。

### 8.3 音频链路

```
ES7210（4 路 MIC 可选，2 通道：MIC + REF）
  → I2S RX（24 kHz，DMA）
  → AFE：AEC / NS / VAD / WakeNet9
  → Opus encode → 编码队列 → WebSocket 上行
  ← WebSocket 下行 → 解码队列 → Opus decode → I2S TX → ES8311 → PCA9557 P1 功放
```

- `AUDIO_INPUT_REFERENCE=true`：输入 2 通道，第二通道作为 AEC 参考；
- 采样率 24 kHz，输入输出一致，避免重采样失真；
- AFE 内存策略 `AFE_MEMORY_ALLOC_MORE_PSRAM`，把大块工作区放 PSRAM；
- 音频输入任务优先级 8，Opus 消费端 4，生产/消费顺序不能倒置。

### 8.4 传感器与状态快照

```
DHT11/演示源 ─┐
QMI8658A ─────┼→ CarMonitor（100 ms，互斥锁）→ CarSnapshot
人员检测 ─────┘                                   ├→ CarUI（500 ms）
                                                 ├→ MCP self.car.*
                                                 └→ CarCloud（5 s / 事件）
```

`CarSnapshot` 是**值拷贝**，调用方拿到的是一致性副本；`valid` + `age_ms` 表达数据新鲜度。
这样 UI、MCP、云端读的是同一份状态，不会出现「界面显示有人、云端显示无人」的撕裂。

### 8.5 GPIO 与电源协同

| 功能 | 控制方式 | 注意 |
| --- | --- | --- |
| 屏背光 | GPIO42 PWM + PCA9557 P0 | 低电平有效，初始化顺序影响亮灭 |
| 功放使能 | PCA9557 P1 | `EnableOutput()` 时同步开/关，避免爆音 |
| 摄像头电源 | PCA9557 P2 | 初始化摄像头前必须拉低 |
| 触摸复位 | 与 LCD RESET 同源 | 按 SW1 会同时复位触摸 IC，不能用来验证软件自愈 |
| 触摸中断 | GPIO10 | 事件驱动，替代 33 ms 轮询，根治漏采 |
| 按键 | GPIO0 / GPIO11 | BOOT 会话，SW3 切屏 |

---

## 9. 巴法云 MQTT 设计

### 9.1 接入参数

| 项目 | 值 | 原因 |
| --- | --- | --- |
| Broker | `bemfa.com:9501` | 普通端口，非 TLS；TLS 会额外吃 30–50 KB 内部 RAM |
| 鉴权 | `clientId = 用户私钥`，username/password 留空 | 巴法云「方式一」 |
| 凭据来源 | NVS `car_cloud/uid` | 不把私钥写死在代码里（当前保留联调兜底常量） |
| 主题前缀 | NVS `car_cloud/prefix`，默认空 | 巴法云主题只能含字母和数字，不能加下划线 |
| Keep Alive | 120 s | 兼顾在线感知与流量 |
| 任务 | CPU0 / 优先级 2 / 栈 5 120 B / 1 s 节拍 | 低于预览，不抢 CPU1 音频 |

### 9.2 主题与负载

| 主题 | 方向 | 周期 | 负载示例 |
| --- | --- | --- | --- |
| `env/up` | 设备→云 | 5 s | `{"v":1,"ts":...,"demo":true,"temp":25.3,"temp_ok":true,...}` |
| `state/up` | 设备→云 | 5 s | `{"v":1,"veh":"PARKED","person":"PRESENT","cnt":1,"conf":0.9,...}` |
| `alarm/set` | 设备→云 | 事件 | `{"v":1,"seq":3,"event":"raise","type":"person_left"}` |
| `online/up` | 设备→云 | 30 s | `{"v":1,"boot":7,"uptime_s":123}` |

设计要点：

- **周期数据用 `/up`**：只更新云端数据，不主动推送给订阅者，降低被限流风险；
- **告警用 `/set`**：巴法云会主动推送给订阅者，告警需要「响」；
- **负载压到 160–200 B**：实测 env（~165 B）能到达，早期 ~450 B 且含嵌套对象的 state
  会丢；因此 state 去掉冗余字段，故障标志压成 `"cam,imu"` 这类短字符串；
- **数值失效发 `null`**：`temp_ok=false` 时 `temp:null`，云端不会把旧值当实时值；
- **浮点用整数拼字符串**：本工程 newlib 为 nano 格式，`%f`/`cJSON %g` 不可靠；
- **断线补发**：告警事件最多缓存 32 条，重连后按 FIFO 补发，丢最旧保最新。

### 9.3 重连策略（避免连接风暴）

`Mqtt::Connect()` 的内部实现会先 `Disconnect()` 销毁客户端再重建。如果每 60 s 调一次，
等于用同一个 `clientId`（用户私钥）反复向巴法云发起连接，容易被 broker 判定为连接风暴
并 RST，表现为「连上几分钟后再也起不来」。

因此 `CarCloud` 只在两种情况下主动 `Connect()`：

1. **从未连上过**（首次）；
2. **已连过但连续断线超过 180 s**（兜底重建一次）。

其余时间交给 `esp-mqtt` 内置自动重连。连接成功后订阅 4 个主题；`OnDisconnected`/`OnError`
只记录日志，不销毁客户端。

### 9.4 云上报在项目中的优先级

- **CPU0 / 优先级 2**：低于预览（3），高于人员检测（1）；不占 CPU1 的音频/AFE/Wi-Fi 实时路径。
- **1 s 节拍、5 s/30 s 发送**：告警变化 ≤1 s 上报，周期数据不追求实时。
- **失败只降级**：云不可用不影响采集、界面、语音、告警；`CarMonitor` 照常运行。
- **延迟 15 s 启动**：避开 OTA/模型加载/AFE 创建的内部 RAM 峰值，失败每 15 s 重试。
- **单条消息有界**：负载控制在 200 B 内，避免巴法云长度限制导致丢消息。

---

## 10. 交互设计

### 10.1 交互入口

| 入口 | 行为 | 说明 |
| --- | --- | --- |
| 语音唤醒词「你好小智」 | 开始/继续会话 | AFE WakeNet9，离线唤醒 |
| BOOT 键（GPIO0） | 切换聊天状态 | 启动阶段未联网时触发 Wi-Fi 配网 |
| SW3 键（GPIO11） | 三页循环 | 聊天 → 数据 → 摄像头 |
| 触摸 | LVGL 事件驱动 | 当前无屏幕按钮，接口保留 |
| MCP `self.car.*` | 语音查询/控制 | LLM 通过 WebSocket 调用设备工具 |
| 巴法云 | 远程查看 | 手机侧看 env/state/alarm/online |

### 10.2 MCP 工具

| 工具 | 作用 | 线程模型 |
| --- | --- | --- |
| `self.car.get_environment` | 查询温湿度/光照 | 读快照，同步返回 |
| `self.car.get_status` | 查询车辆/人员/告警/网络 | 读快照，同步返回 |
| `self.car.get_alert` | 查询活跃告警 | 读快照，同步返回 |
| `self.car.confirm_alert` | 确认告警（静音 30 s） | 投命令队列，异步生效 |
| `self.car.toggle_vehicle_state` | 行驶/停车切换 | 投命令队列，异步生效 |

另有 `self.camera.take_photo`、`self.dht11.*`、`self.myinfo.*`、`self.screen.*`、
`self.audio_speaker.*` 等通用工具。所有工具都只读快照或投队列，不直接跨任务操作硬件。

### 10.3 数据页布局

```
┌──────────────────────────────────────────────┐
│  车辆状态（通栏 52 px）                       │
├──────────────────────┬───────────────────────┤
│  环境数据             │  人员检测              │
│  温度 / 湿度 / 光照   │  人数 / 置信度         │
├──────────────────────┴───────────────────────┤
│  告警（通栏 48 px）                           │
└──────────────────────────────────────────────┘
```

摄像头页是 320×240 的 4:3 预览区 + 检测框叠加层，整页不滚动。
检测框坐标来自 320×240 源画面，与预览内区 1:1，无需换算。

---

## 11. 可靠性与降级矩阵

| 故障 | 检测方式 | 降级行为 | 不影响 |
| --- | --- | --- | --- |
| 触摸 IC 不应答 | I2C 读 ID 失败 | 跳触摸注册，打印错误 | 显示、语音、云 |
| 摄像头初始化失败 | `esp_camera_init` 返回错误 | 无预览/无检测，UI 保持占位 | 音频、云、告警框架 |
| 人员检测模型缺失/损坏 | 分区校验、`get_raw_model()` | 不启动检测，不上报「无人」 | 预览、音频、云 |
| IMU 缺失/读失败 | WHO_AM_I、连续坏帧 | `imu_fault=true`，不参与告警 | 其他全部 |
| 巴法云连不上 | MQTT 状态 | 本地缓存告警 32 条，自动重连 | 采集/界面/语音 |
| OTA 检查失败 | HTTP/TLS 错误 | 记录日志，继续启动 | 所有本地功能 |
| 内部 RAM 低 | `heap_caps_get_minimum_free_size` | 新任务创建失败即降级，不重启 | 已有任务 |
| 栈溢出 | FreeRTOS 日志 | 定位任务→加栈/迁 PSRAM→复测 | 整机稳定性 |
| 服务端会话残留 | 第二次唤醒 10 s 无 hello | `goodbye` 主动关闭 + 30 s 静默兜底 | 下一次唤醒 |
| WebSocket 握手丢 101 | 握手超时 | 回调提前注册 + 失败位快速返回 | 重连 |

### 11.1 防「软复位死循环」

- 触摸句柄必须 `nullptr` 初始化并检查返回值；
- 触摸驱动写 `0x86=0x00` 禁止自动 Monitor；
- I2C 设备探测失败只返回 false，不 `while(1)` 等；
- `ESP_ERROR_CHECK` 不用于「从机可能缺席」的路径；
- 任何初始化失败都继续启动，保留可用的功能。

---

## 12. 自测与验收

### 12.1 构建与烧录

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

- 目标芯片：ESP32-S3；
- Flash 16 MB QIO，PSRAM Octal 80 MHz；
- 应用固件约 5.86 MB，OTA 槽 6 MB；
- 首次烧录需同时写入 `model`（ESP-SR）与 `pedestrian_det`（esp-dl）分区。

### 12.2 上板自测项（2026-10-22）

| # | 检查项 | 判据 |
| --- | --- | --- |
| 1 | 冷启动 | 无 `Guru Meditation`、无重启循环 |
| 2 | 显示 | 三页可切换，数据页数值刷新 |
| 3 | 触摸 | 日志出现 `Monitor ctrl (0x86) = 0x00`，连续点击无漏采 |
| 4 | 摄像头 | 预览出图，`Preview stats` 有 fps |
| 5 | 人员检测 | 模型加载成功，检测日志有框/分数 |
| 6 | 语音 | 唤醒→对话→TTS→Idle，二次唤醒正常 |
| 7 | 网络 | Wi-Fi 连接、WebSocket 会话建立 |
| 8 | OTA | 版本检查成功，无内存失败 |
| 9 | 巴法云 | `env/state/online` 到达云端，告警事件 ≤1 s |

串口关键日志：

```
CarCloud: 巴法云已连接（任务栈余量 N B）
CarCloud: 上报负载长度：env=... B，state=... B
PersonDetect: 模型加载完成：PSRAM 占用 ... | 内部 RAM ...
CarUIHelper: Preview stats: fps=...
Application: [DIAG] state=Idle wake_word=ON voice_proc=ON internal_heap=...
```

---

## 13. 关键文件索引

| 类别 | 文件 |
| --- | --- |
| 应用启动/状态机 | `main/application.cc` / `.h` |
| 板级初始化 | `main/boards/grw-esp32s3-board/grw_esp32s3_board.cc` |
| 引脚定义 | `main/boards/grw-esp32s3-board/config.h` |
| 车载快照 | `components/car_monitor/car_snapshot.h` |
| 监测任务/告警 | `components/car_monitor/car_monitor.cc` |
| 车载 UI | `components/car_monitor/car_ui.cc` / `car_camera_ui.cc` |
| 屏幕管理 | `main/screen_manager.cc` |
| UI 接线/预览 | `main/car_ui_helper.cc` |
| 人员检测 | `main/car_person_detect.cc` |
| 巴法云 | `main/car_cloud.cc` |
| 摄像头驱动 | `main/boards/common/esp32_camera.cc` |
| 音频服务 | `main/audio/audio_service.cc` |
| AFE 唤醒 | `main/audio/wake_words/afe_wake_word.cc` |
| LVGL 显示 | `main/display/lcd_display.cc` |
| IMU | `main/qmi8658.cc` |
| 分区表 | `partitions.csv` |
| 内存方案 | `docs/ESP32_MEMORY_IMPROVEMENT_PLAN.md` |
| 问题解决记录 | `docs/TROUBLESHOOTING.md` |
| 面试解说词 | `docs/INTERVIEW_GUIDE.md` |

---

## 14. 后续演进

1. **真实环境传感器**：DHT11 单总线/光照 ADC 接入 `UpdateEnvironmentData()`，
   启用温度/湿度告警（阈值与滞回已定义）。
2. **检测模型升级**：`pedestrian_det` 分区留了 1 MB，可替换更大输入分辨率/更高精度模型。
3. **UI 恢复屏幕按钮**：触摸中断稳定后，重新启用摄像头页三颗按钮。
4. **PSRAM DMA 直推**：若要继续提帧率，需绕开 `esp_lcd` 自建 SPI 通路并设置
   `SPI_TRANS_DMA_USE_PSRAM`，但受 PSRAM DMA 速度上限约束。
5. **多设备云端**：用 NVS `car_cloud/prefix`（仅字母数字）区分设备主题。
6. **OTA 资源治理**：应用已占 6 MB 槽 93%，后续加功能需裁剪字体/音频资源或调整分区。

---

## 15. 一句话总结

这个项目的难点不在「把外设驱动起来」，而在 **512 KB 内部 SRAM、80 MHz SPI、双核实时音频、
摄像头共享帧缓冲、云端弱网** 这五个约束同时存在时，仍然让界面流畅、语音可用、数据可上报。
所有设计决策最终都落到三件事：**减少每帧字节数、减少内部堆峰值、把阻塞移出实时路径**。
