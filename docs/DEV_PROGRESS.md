# car-ai-s3 开发进度与交接

> 面向新对话窗口/接手的同学：先读本文件，再按需跳转。
> 项目总览：[../README.md](../README.md)
> 架构与内存账本：[ARCHITECTURE.md](ARCHITECTURE.md)
> 问题解决记录：[TROUBLESHOOTING.md](TROUBLESHOOTING.md)
> 面试解说词：[INTERVIEW_GUIDE.md](INTERVIEW_GUIDE.md)
>
> 工程：`car-ai-s3`　板：GRW ESP32-S3　更新：2026-10-22

---

## 一、当前状态

| 事项 | 状态 |
| --- | --- |
| 显示 / LVGL 三屏 | 已完成并上板 |
| 数据页 + 摄像头页拆页、禁滚动 | 已完成并上板 |
| 摄像头实时预览 + 检测框叠加 | 已完成并上板 |
| 人员遗留告警 | 已完成并上板 |
| 小智语音会话 / 二次唤醒 | 已完成并上板 |
| 巴法云 env/state/alarm/online | 已完成，云端可达 |
| 巴法云延迟启动 + 失败重试 | 已完成 |
| OTA 双槽 + 版本检查 | 已接入 |
| QMI8658A 运动检测 | 已接入（辅助状态，不参与告警） |
| 真实 DHT11 / 光照传感器 | 未接，当前为演示源 |
| 温度/湿度告警 | 阈值已定义，函数未启用 |
| 摄像头页屏幕按钮 | 已移除，回调接口保留 |
| 内部 RAM 后续优化 | 方案见 `ESP32_MEMORY_IMPROVEMENT_PLAN.md`，暂不实施 |

---

## 二、关键设计事实

- 触摸：FT6336U，I2C1 0x38，INT=GPIO10（事件驱动），驱动写 `0x86=0x00` 禁止自动 Monitor。
- 触摸坐标：`x_max=320` / `y_max=480` / `swap_xy=1` / `mirror_x=0` / `mirror_y=1`。
- 触摸阈值：20（芯片上电默认 15；组件默认 48）。
- LCD：ST7796S，SPI3 80 MHz，480×320，RGB565，LVGL 15 行双缓冲（28.8 KB）。
- 摄像头：GC0308 DVP，QVGA 320×240 RGB565，2 帧缓冲在 PSRAM。
- 音频：ES8311 DAC + ES7210 ADC，I2S 24 kHz，PCA9557 P1 功放使能。
- 共享 I2C1：PCA9557 0x19 / ES8311 0x18 / ES7210 0x82 / FT6336U 0x38 / QMI8658A 0x6A。
- 分区：双 6 MB OTA；`model` 960 KB（ESP-SR）；`pedestrian_det` 1 MB（esp-dl）。
- 巴法云：`bemfa.com:9501`，clientId=用户私钥，主题只能字母数字。

---

## 三、已落地的关键修复（摘要）

| 问题 | 修复 | 详细 |
| --- | --- | --- |
| 二次唤醒一直等待响应 | `goodbye` 分支 + `Schedule()` 优雅关闭 + 8 s/30 s 静默兜底 | TROUBLESHOOTING §1 |
| 拍照后预览卡死 | `fb_mutex_` 保护 + 三条路径归还 `fb_` + 排水再 join | §2 |
| 巴法云/OTA 随机失败 | 云上报延迟 15 s + 失败重试 | §3 |
| `car_cloud` 栈溢出 | 栈 2560 → 5120 B + 高水位日志 | §4 |
| WebSocket 握手超时 | 回调注册先于 `tcp_->Send()` + 失败位 | §5 |
| 触摸 Monitor 崩溃循环 | 写 `0x86=0x00` + 句柄空检查 + 失败跳过注册 | §6 |
| 全屏滚动卡顿 | 拆页 + 禁滚动 + 15 行双缓冲 | §7 |
| 触摸间歇失效 | 缓冲减半 + 中断驱动 | §8 |
| AFE 溢出刷屏 | Opus 优先级 2→4 + PSRAM 栈兜底 | §9 |
| MQTT 连接风暴 | 不频繁重建，仅首次/长期断线重连 | §10 |

---

## 四、诊断开关

| 开关 | 位置 | 用途 |
| --- | --- | --- |
| `kEnableImuMotion` | `main/application.cc` | 关闭 IMU，二分触摸/唤醒问题 |
| `kEnableCloudUpload` | `main/application.cc` | 关闭云上报，二分内存/OTA |
| `kEnablePersonDetect` | `main/car_person_detect.cc` | 关闭推理，二分 CPU/PSRAM 抢占 |
| `kDumpModelMemory` | `main/car_person_detect.cc` | 打印 esp-dl 逐层内存 |
| `kLogEveryDetection` | `main/car_person_detect.cc` | 每次检测打印框/分数 |

---

## 五、构建与烧录提示

- 目标：ESP32-S3，ESP-IDF 5.5.3，Flash 16 MB QIO，PSRAM Octal 80 MHz。
- 应用固件约 5.86 MB；首次烧录需写 `model` 与 `pedestrian_det` 两个模型分区。
- 本机 `ccache` 曾挂死：若长时间卡在某个 `.obj` 且无 gcc 子进程，
  设 `CCACHE_DISABLE=1` / `IDF_CCACHE_ENABLE=0` 后重跑，不要傻等。
- CH340K 串口不稳定时，可改用板载原生 USB 口烧录。
- 软复位不会给触摸 IC 断电；触摸进入 Monitor 时需整板断电或按 SW1（CTP_RST 与 EN 同源）。

---

## 六、managed_components 本地补丁记录

> `managed_components/` 被 `.gitignore` 忽略，删除目录重新拉取依赖会还原上游代码，
> 下列补丁会丢失，必须记录。

| 日期 | 文件 | 补丁 | 原因 |
| --- | --- | --- | --- |
| 2026-10-21 | `78__esp-ml307/src/web_socket.cc` | `OnStream()` / `OnDisconnected()` 注册与 `xEventGroupClearBits()` 移到 `tcp_->Send(request)` 之前 | 服务端 101 可能在回调注册前到达，`ReceiveTask` 因 `stream_callback_` 为空丢数据，表现为 10 s 握手超时 |
| 2026-10-22 | 同上 | `OnDisconnected` 在 `!handshake_completed_` 时置 `HANDSHAKE_FAILED_BIT` | 握手期服务端主动断开时旧代码不置失败位，只能白等 10 s |

---

## 七、上板自测记录（2026-10-22）

| # | 项目 | 结果 |
| --- | --- | --- |
| 1 | 冷启动无重启循环 | PASS |
| 2 | 三页切换 / 数据页刷新 | PASS |
| 3 | 触摸 `0x86=0x00` / 连续点击 | PASS |
| 4 | 摄像头预览 fps | PASS |
| 5 | 人员检测模型加载 / 推理 | PASS |
| 6 | 唤醒 → 对话 → TTS → Idle | PASS |
| 7 | 二次唤醒 | PASS |
| 8 | OTA 版本检查 | PASS |
| 9 | 巴法云 env/state/online | PASS |

> 判定以串口日志为准；云端周期连续性建议在长时间运行场景继续观察。

---

## 八、关键文件索引

| 类别 | 文件 |
| --- | --- |
| 应用启动/状态机 | `main/application.cc` / `.h` |
| 板级初始化 | `main/boards/grw-esp32s3-board/grw_esp32s3_board.cc` |
| 引脚定义 | `main/boards/grw-esp32s3-board/config.h` |
| 业务/快照 | `components/car_monitor/car_monitor.cc` / `car_snapshot.h` |
| UI | `components/car_monitor/car_ui.cc` / `car_camera_ui.cc` |
| 屏幕管理 | `main/screen_manager.cc` |
| UI/预览接线 | `main/car_ui_helper.cc` |
| 人员检测 | `main/car_person_detect.cc` |
| 巴法云 | `main/car_cloud.cc` |
| 摄像头驱动 | `main/boards/common/esp32_camera.cc` |
| 音频 | `main/audio/` |
| 分区表 | `partitions.csv` |

---

## 九、下一步

1. 接真实 DHT11/光照传感器，启用温度/湿度告警；
2. 触摸稳定后恢复摄像头页按钮；
3. 观察巴法云 5 s 周期长时间连续性；
4. 如需继续提帧率，评估绕开 `esp_lcd` 的 PSRAM DMA 通路；
5. 评估应用固件 93% 占槽的资源裁剪方案。
