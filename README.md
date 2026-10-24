# car-ai-s3 · 车载 AI 行车状态监测与语音交互终端

基于 **ESP32-S3** 的车载智能终端：本地环境监测、摄像头人员遗留检测、LVGL 三屏交互、
离线语音唤醒 + 小智 AI 对话、巴法云 MQTT 远程查看。

> 代码基线：小智 AI `xiaozhi-esp32` 1.9.4 · ESP-IDF 5.5.3 · LVGL 9.3.0 · ESP-SR · esp-dl
> 板型：GRW ESP32-S3（ESP32-S3-WROOM-1-N16R8）

---

## 1. 项目能做什么

| 能力 | 实现 | 状态 |
| --- | --- | --- |
| 环境监测 | 温湿度/光照数据通道，`CarSnapshot` 100 ms 刷新 | 演示源已通，真实传感器接口预留 |
| 人员遗留检测 | GC0308 摄像头 + esp-dl `pedestrian_detect`（PICO_S8_V1） | 已上板 |
| 本地告警 | 停车 15 s 后连续 3 次检测到人触发；确认后静音 30 s | 已上板 |
| 车载界面 | 数据页 / 摄像头页 / 聊天页，SW3 循环切换，两页禁滚动 | 已上板 |
| 实时预览 | 摄像头页 320×240、10 FPS，检测框叠加 | 已上板 |
| 语音交互 | AFE WakeNet9「你好小智」、Opus 编解码、WebSocket 会话 | 已上板 |
| 语音查询 | MCP `self.car.*`：环境、状态、告警、确认、车辆启停 | 已上板 |
| 云端上报 | 巴法云 MQTT：`env` / `state` / `alarm` / `online` | 已上板 |
| OTA | 双 6 MB OTA 槽，版本检查与回滚 | 已接入 |
| 姿态检测 | QMI8658A 25 Hz，仅作辅助状态，不参与告警 | 已上板 |

### 明确边界

- **环境数据当前是演示源**：`CarMonitor::Init(true)` 使用 `VirtualSensors`，
  `Dht11Sensor` 也是虚拟数据。真实 DHT11/光照传感器接入点已留在
  `CarMonitor::UpdateEnvironmentData()`，不影响 UI、MCP、云端和告警框架。
- **温度/湿度告警未启用**：阈值与滞回参数已定义，函数仍是占位实现。
- **屏幕按钮已移除**：摄像头页原有三颗触摸按钮因早期触摸不稳定被移除，回调接口保留；
  当前交互以 SW3 切屏、BOOT 键会话控制、语音/MCP 为主。
- **巴法云只传监测数据**，语音链路仍走小智 WebSocket 服务。

---

## 2. 硬件

| 模块 | 型号 / 参数 | 接口 |
| --- | --- | --- |
| 主控 | ESP32-S3-WROOM-1-N16R8，双核 LX7 240 MHz，512 KB SRAM | — |
| 存储 | 8 MB Octal PSRAM（80 MHz）+ 16 MB QIO Flash | — |
| 显示 | ST7796S，480×320，RGB565 | SPI3 80 MHz（MOSI 40 / SCLK 41 / DC 39 / BL 42） |
| 触摸 | FT6336U | I2C1 0x38，INT=GPIO10，400 kHz |
| 摄像头 | GC0308 DVP，QVGA 320×240，RGB565，XCLK 24 MHz | D0~D7 / VSYNC 3 / HREF 46 / PCLK 7 |
| 音频 DAC | ES8311 | I2S 24 kHz（MCLK 38 / BCLK 14 / WS 13 / DOUT 45） |
| 音频 ADC | ES7210，4 路 MIC 可选，MIC + AEC 参考双通道 | I2S（DIN 12） |
| IO 扩展 | PCA9557 0x19：屏背光 / 功放 / 摄像头电源 | I2C1 |
| 姿态 | QMI8658A 0x6A，25 Hz | I2C1 |
| 按键 | BOOT=GPIO0，SW3=GPIO11 | GPIO |

共享 I2C1（SDA=1 / SCL=2）挂载 PCA9557、ES8311、ES7210、FT6336U、QMI8658A 与
GC0308 SCCB；SPI3 只用于 LCD；I2S 独立音频链路。

---

## 3. 软件架构

```
┌────────────────────────────────────────────────────────────┐
│ Application：设备状态机 / 主事件循环 / WebSocket / OTA      │
│ ScreenManager：聊天页 · 数据页 · 摄像头页                  │
├────────────────────────────────────────────────────────────┤
│ CarMonitor：CarSnapshot 一致快照 / 告警状态机 / 命令队列    │
│ CarMonitorHelper：MCP self.car.* / 人员结果入口            │
├────────────────────────────────────────────────────────────┤
│ CarUIHelper    CarPersonDetect    CarCloud    AudioService  │
│ 数据页 500ms   esp-dl 推理        巴法云 MQTT  AFE/Opus     │
├────────────────────────────────────────────────────────────┤
│ GRW Board：I2C1 / SPI3 / I2S / DVP / GPIO / PCA9557         │
└────────────────────────────────────────────────────────────┘
```

### FreeRTOS 任务（核心）

| 任务 | 核 | 优先级 | 栈 | 周期 | 职责 |
| --- | --- | --- | --- | --- | --- |
| `audio_input` | — | 8 | 6 KB | I2S DMA | 音频输入，硬实时 |
| `car_monitor` | — | 5 | 4 KB | 100 ms | 环境/告警状态机 |
| `car_ui_update` | — | 5 | 4 KB | 500 ms | 数据页刷新 |
| `opus_codec` | — | 4 | 26 KB | 队列 | Opus 编解码 |
| `audio_output` | — | 4 | 4 KB | 队列 | TTS 播放 |
| `LVGL` | CPU1 | 4 | — | 33 ms | 渲染/推屏 |
| `main_event_loop` | — | 3 | 8 KB | 事件组 | 状态机/会话 |
| `cam_preview` | CPU0 | 3 | 4 KB | 100 ms | 预览取帧 |
| `audio_communication` | — | 3 | 4 KB | AFE | 音频处理搬运 |
| `audio_detection` | — | 3 | 4 KB | AFE | 唤醒词检测 |
| `car_cloud` | CPU0 | 2 | 5 KB | 1 s | 巴法云上报 |
| `imu_motion` | CPU0 | 2 | 2.5 KB | 40 ms | 运动检测 |
| `encode_wake_word` | — | 2 | 28 KB（PSRAM） | 事件 | 唤醒词编码 |
| `person_detect` | CPU0 | 1 | 4 KB | 500/1000 ms | esp-dl 推理 |

> 设计原则：硬实时音频最高；消费端不低于生产端；图像/云/推理钉 CPU0，
> LVGL 钉 CPU1；回调只置事件位，耗时操作统一 `Schedule()` 到主循环。

---

## 4. 内存与性能设计

| 资源 | 用量 / 策略 |
| --- | --- |
| 内部 SRAM | 512 KB；开机静态约 253 KB，动态堆约 259 KB；64 KB 预留 DMA；普通内部堆约 195 KB |
| 内部 RAM 水位 | 稳态空闲 32–33 KB，全周期最低 9–17 KB |
| LVGL 绘制缓冲 | 480×15×2 B×双缓冲 = **28.8 KB**，必须在内部 DMA RAM |
| PSRAM 8 MB | 摄像头帧 2×150 KB、预览双缓冲 2×150 KB、检测帧 150 KB、LVGL 图片缓存 2 MB、esp-dl 张量、AFE 工作区、唤醒词栈 28 KB |
| Flash 16 MB | 双 OTA 各 6 MB（固件 5.86 MB）、ESP-SR 模型 960 KB、人员检测模型 1 MB、NVS/otadata/phy |
| 屏幕带宽 | 480×320×2 = 307 200 B/帧，SPI 80 MHz 纯传输约 30.7 ms；拆页 + 禁滚动后脏区降到几 KB |
| 云端 | 巴法云 9501 非 TLS；env/state 5 s，online 30 s，告警 ≤1 s、断线缓存 32 条 |

关键优化：

1. LVGL 缓冲 30 行 → 15 行，省 28.8 KB，触摸恢复灵敏；
2. 车载界面拆成数据页/摄像头页并禁用滚动，解决 SPI 带宽卡顿；
3. 数据页 500 ms 刷新 + 文本去重，减少无意义重绘；
4. 预览只在摄像头页取帧（10 FPS），会话期人员检测降频到 1 s；
5. 巴法云延迟 15 s 启动，避开 OTA/模型/AFE 的内部 RAM 峰值；
6. 会写 NVS/OTA 的任务绝不使用 PSRAM 栈。

详细账本与取舍见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

---

## 5. 快速开始

### 5.1 环境

- ESP-IDF **5.5.3**（VS Code ESP-IDF 扩展或命令行）
- 目标：`esp32s3`
- Python 依赖由 ESP-IDF 管理

### 5.2 构建与烧录

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

首次烧录会同时写入：

- `0x10000`：`srmodels/srmodels.bin`（ESP-SR 唤醒模型，约 291 KB）
- `0xD00000`：`espdl_models/pedestrian_detect.espdl`（人员检测模型，约 435 KB）

分区表见 [partitions.csv](partitions.csv)。

### 5.3 配置

- **Wi-Fi**：首次上电按小智配网流程连接 2.4 GHz 网络；启动阶段 BOOT 键可触发重新配网。
- **巴法云**：在 NVS 命名空间 `car_cloud` 写入：

| 键 | 说明 |
| --- | --- |
| `uid` | 巴法云用户私钥（作为 MQTT clientId） |
| `prefix` | 可选主题前缀，只能含字母和数字 |

默认主题：`env` / `state` / `alarm` / `online`。需在巴法云控制台提前创建。
代码中保留了联调兜底私钥，正式部署应改为纯 NVS 配置。

- **小智 AI**：使用 xiaozhi.me 控制台绑定设备，选择 Qwen 实时模型。

---

## 6. 交互方式

| 操作 | 效果 |
| --- | --- |
| 说「你好小智」 | 唤醒并开始语音会话 |
| BOOT 键 | 切换聊天状态；启动阶段未联网时触发配网 |
| SW3 键 | 聊天页 → 数据页 → 摄像头页 → 聊天页 |
| 语音「车里温度多少」 | LLM 调用 `self.car.get_environment` |
| 语音「车里有人吗」 | LLM 调用 `self.car.get_status` |
| 语音「确认告警」 | `self.car.confirm_alert`，静音 30 s |
| 语音「切换到停车状态」 | `self.car.toggle_vehicle_state` |
| 巴法云控制台/手机 | 查看 `env` / `state` / `alarm` / `online` |

MCP 工具全部只读快照或投命令队列，不直接跨任务操作硬件。

---

## 7. 云端数据格式

| 主题 | 周期 | 示例 |
| --- | --- | --- |
| `env/up` | 5 s | `{"v":1,"ts":123456,"demo":true,"temp":25.3,"temp_ok":true,"hum":58.0,"hum_ok":true,"light":42.5,"light_ok":true}` |
| `state/up` | 5 s | `{"v":1,"veh":"PARKED","person":"PRESENT","cnt":1,"conf":0.91,"moving":0,"imu_ok":1,"alert":"person_left","alert_ok":0,"fault":"none","wifi":1,"cloud":1,"demo":1}` |
| `alarm/set` | 事件 | `{"v":1,"ts":123456,"seq":3,"event":"raise","type":"person_left"}` |
| `online/up` | 30 s | `{"v":1,"ts":123456,"boot":7,"uptime_s":123}` |

设计要点：

- 周期数据用 `/up`（只更新云端，不推送订阅者），告警用 `/set`（主动推送）；
- 负载控制在 200 B 内，规避巴法云单条消息长度限制；
- 数据失效时字段发 `null`，不把旧值当实时值；
- 断线期间告警最多缓存 32 条，重连后 FIFO 补发；
- MQTT 客户端不频繁销毁重建，避免连接风暴。

---

## 8. 文档

| 文档 | 内容 |
| --- | --- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 框架、启动时序、FreeRTOS、内存账本、外设协同、数据流、MQTT 设计 |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | 问题现象、根因链、修复、验证与经验总结 |
| [docs/INTERVIEW_GUIDE.md](docs/INTERVIEW_GUIDE.md) | 30 秒 / 1 分钟 / 3 分钟项目解说词与高频追问 |
| [docs/DEV_PROGRESS.md](docs/DEV_PROGRESS.md) | 开发进度、板级事实、managed_components 补丁记录 |
| [docs/ESP32_MEMORY_IMPROVEMENT_PLAN.md](docs/ESP32_MEMORY_IMPROVEMENT_PLAN.md) | 内部 RAM 后续优化方案（暂不实施） |
| [docs/esp32-memory-guide/esp32-memory-guide.html](docs/esp32-memory-guide/esp32-memory-guide.html) | ESP32-S3 内存图文指南 |
| [docs/mcp-usage.md](docs/mcp-usage.md) | MCP 物联网控制用法 |
| [docs/mcp-protocol.md](docs/mcp-protocol.md) | MCP 协议交互流程 |
| [docs/mqtt-udp.md](docs/mqtt-udp.md) | MQTT + UDP 混合通信协议 |
| [docs/websocket.md](docs/websocket.md) | 小智 WebSocket 协议 |

---

## 9. 项目结构

```
car-ai-s3/
├── main/
│   ├── application.cc/.h          # 启动、主事件循环、状态机
│   ├── car_cloud.cc/.h            # 巴法云 MQTT
│   ├── car_monitor_helper.cc/.h   # 快照封装 + self.car.* MCP
│   ├── car_person_detect.cc/.h    # esp-dl 人员检测
│   ├── car_ui_helper.cc/.h        # 数据页刷新 + 摄像头预览
│   ├── screen_manager.cc/.h       # 三屏管理
│   ├── dht11_sensor.cc/.h         # 温湿度数据源（当前虚拟）
│   ├── qmi8658.cc/.h              # QMI8658A IMU
│   ├── myinfo.cc/.h               # 语音播报信息
│   ├── audio/                     # AudioService / AFE / Opus / 编解码
│   ├── boards/
│   │   ├── grw-esp32s3-board/     # 本板引脚与初始化
│   │   └── common/                # 摄像头、按键、板级基类
│   ├── display/                   # LCD/OLED + LVGL 端口
│   └── protocols/                 # WebSocket / MQTT
├── components/
│   ├── car_monitor/               # 快照、业务、数据页、摄像头页
│   └── esp_lcd_touch_ft6x36/      # FT6336U 驱动（含 0x86 修复）
├── docs/                          # 架构、问题、面试、协议文档
├── partitions.csv                 # 16 MB 分区表
├── sdkconfig.defaults             # 固化配置
└── sdkconfig.defaults.esp32s3     # S3 专用配置
```

---

## 10. 已知限制与后续计划

- 接入真实 DHT11 / 光照传感器，启用温度/湿度告警（阈值与滞回已定义）；
- 触摸稳定后恢复摄像头页三颗屏幕按钮；
- 应用固件已占 6 MB OTA 槽约 93%，后续加功能需裁剪资源或调整分区；
- 如需继续提帧率，只能减少每帧字节数，或绕开 `esp_lcd` 自建 PSRAM DMA 通路；
- 多设备部署使用 NVS `car_cloud/prefix` 区分主题。

---

## 11. 上游与许可

- 语音交互、协议与板级框架基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT）；
- 人员检测基于乐鑫 [esp-dl](https://github.com/espressif/esp-dl) 与
  `pedestrian_detect`（PICO_S8_V1）；
- 语音前端基于乐鑫 ESP-SR（AFE + WakeNet9）；
- 界面基于 LVGL 9.3.0 与 `esp_lvgl_port`；
- 本项目沿用 MIT 许可证，见 [LICENSE](LICENSE)。

> 上游 README 中的多板型图片与硬件清单不再维护；本项目的板级事实以
> [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) 和
> [docs/DEV_PROGRESS.md](docs/DEV_PROGRESS.md) 为准。
