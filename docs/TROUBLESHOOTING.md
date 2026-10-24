# car-ai-s3 问题解决总结

> 记录本项目从「能点亮」到「能稳定跑完整链路」过程中定位过的典型问题。
> 每条按「现象 → 根因 → 修复 → 验证 → 经验」组织，便于复盘和面试讲解。
> 日期：2026-10-22

---

## 目录

1. 第二次唤醒后一直「等待响应」
2. 拍照后摄像头预览卡死
3. 巴法云启动失败 / OTA 随机失败
4. `car_cloud` 栈溢出导致重启循环
5. WebSocket 握手 10 s 超时
6. 触摸 IC 进入 Monitor 模式导致崩溃重启
7. 全屏滚动卡顿：SPI 带宽瓶颈
8. 触摸间歇性失效：内部 RAM 低水位
9. AFE 环形缓冲溢出刷屏与任务创建失败
10. 巴法云 MQTT 连接风暴
11. 软复位后触摸失效 / 屏幕全黑
12. 浮点格式化失效（nano newlib）
13. 模型/摄像头/云任务互相抢内部 RAM
14. managed_components 本地补丁记录
15. 问题定位方法沉淀

---

## 1. 第二次唤醒后一直「等待响应」

### 现象

第一次唤醒小智，对话正常；对 AI 说「拜拜」后，第二次唤醒能进入监听，但一直提示
「等待响应」，服务端不下发 hello/TTS，最终超时。

### 根因

服务端下发 `goodbye` 后，设备端没有处理分支，只是被动等待 TCP 断开。服务端侧会话
没有立即释放，残留会话占着同一个设备连接，第二次 WebSocket 握手成功但 server hello
10 s 无响应。

同时还有两个放大因素：

- `Listening` 状态下唤醒词检测是关闭的，服务端不关会话，设备就永远回不到可唤醒状态；
- WebSocket 握手期若服务端主动断开，旧代码不置失败位，只能白等 10 s。

### 修复

1. `main/application.cc` 增加 `goodbye` 分支：

```cpp
} else if (strcmp(type->valuestring, "goodbye") == 0) {
    ESP_LOGI(TAG, "Server requested goodbye, closing audio channel");
    Schedule([this]() {
        protocol_->CloseAudioChannel();
    });
}
```

2. 必须经 `Schedule()` 切到主事件循环再关闭。回调运行在 WebSocket 的 `ssl_receive`
   任务里，直接销毁 WebSocket 会 use-after-free 或自等死锁。
3. 增加本地静默兜底：
   - AutoStop 模式 8 s；
   - Realtime 模式 30 s（服务端沉默/发送链路死亡时唯一的本地逃生）；
   - ManualStop 不做兜底。
4. `managed_components/78__esp-ml307/src/web_socket.cc` 调整握手时序：
   - `OnStream()` / `OnDisconnected()` 注册移到 `tcp_->Send(request)` 之前；
   - `OnDisconnected` 在 `!handshake_completed_` 时置 `HANDSHAKE_FAILED_BIT`。

### 验证

- 连续「唤醒 → 说拜拜 → 立刻再次唤醒」多轮，第二次能正常收到 hello 并进入 Listening；
- 关闭服务端/断网时，30 s 内自动回 Idle，唤醒词检测重新开启；
- 串口出现 `Server requested goodbye, closing audio channel`。

### 经验

会话类协议必须显式处理**服务端主动结束**，不能依赖 TCP 断开；实时回调线程不能直接
销毁所属对象，必须把「关闭动作」切到拥有该对象的主循环。

---

## 2. 拍照后摄像头预览卡死

### 现象

调用 `self.camera.take_photo` 拍照上传后，摄像头页画面停在最后一帧，不再刷新；
严重时主事件循环卡住。

### 根因

1. `Explain()` 使用 `fb_` 做 JPEG 编码，但只在下次 `Capture()` 时才归还；上传结束后
   预览任务一直拿到旧帧，表现为「卡死」。
2. 上传失败路径先 `join()` 编码线程，而编码线程可能阻塞在
   `xQueueSend(jpeg_queue, ..., portMAX_DELAY)`；主循环没有排水，双方互等 → 死锁。
3. 预览/推理任务在 `CopyFrameLocked()` 中持 `fb_mutex_` 拷贝 `fb_`，`Explain()` 无锁
   归还时可能与 `memcpy` 并发，导致撕裂帧或 use-after-free。

### 修复

`main/boards/common/esp32_camera.cc`：

```cpp
auto release_fb = [this]() {
    std::lock_guard<std::mutex> lock(fb_mutex_);
    if (fb_ != nullptr) {
        esp_camera_fb_return(fb_);
        fb_ = nullptr;
    }
};
```

- 早退（URL 未设置）、上传失败、上传成功三条路径都调用 `release_fb()`；
- 失败路径先循环 `xQueueReceive` 排水到 `nullptr` 终止块，再 `join()`；
- 预览/推理取帧遵守同一把锁：`fb_` 非空则只读借用，自己抓的帧用完即还。

### 验证

- 拍照后 1 s 内预览恢复；
- 连续拍照 20 次无卡死、无花屏；
- 上传失败（断网/URL 错误）后预览仍恢复。

### 经验

共享单帧缓冲的驱动必须明确「谁持有、何时归还」；任何早退路径都要归还资源。
队列满时的 `join()` 是经典死锁点，先排水再 join。

---

## 3. 巴法云启动失败 / OTA 随机失败

### 现象

开启巴法云上传后，OTA 版本检查大多数失败、偶尔成功；Wi-Fi 连接正常；
AFE 检测任务偶发创建失败。关闭云上报后 OTA 恢复正常。

### 根因

启动窗口内的内部 RAM 竞争：

| 模块 | 瞬时内部 RAM | 特点 |
| --- | --- | --- |
| `PedestrianDetect` 模型加载 | ~21 KB | 加载瞬间最低水位仅约 4 KB |
| OTA TLS 握手 | ~20 KB | 其中约 16 KB 必须是连续块 |
| MQTT 客户端初始化 | 数 KB | 任务栈 + 连接缓冲 |

三者叠加时，OTA 的 16 KB 连续块拿不到，表现为「大多数失败、偶尔成功」；
AFE 的 4 KB 连续块也可能失败。

### 修复

- 把 `CarCloud::Start()` 从启动序列移到一次性 `esp_timer`，延迟 15 s；
- 失败后每 15 s 重试，成功后不再重装定时器；
- 人员检测仍然放在 `StartNetwork()` 之后；
- 云上报任务 CPU0/优先级 2/栈 5120 B，不与 CPU1 音频抢核。

### 验证

- 连续冷启动 5 次，OTA 版本检查均成功；
- 巴法云在开机约 15 s 后连接成功；
- AFE 检测任务无创建失败日志；
- 串口出现 `Car cloud upload scheduled to start in 15 s (retry on failure)`。

### 经验

启动阶段的「内存峰值」比「平均占用」更致命。把非关键且可延迟的初始化移出启动窗口，
是最便宜、风险最低的稳定性优化。

---

## 4. `car_cloud` 栈溢出导致重启循环

### 现象

设备拿到 IP 后立刻重启，串口反复出现：

```
***ERROR*** A stack overflow in task car_cloud
```

### 根因

`car_cloud` 原栈 2 560 B。启动路径包含
`nvs_get/set` + `esp_mqtt_client_init` + 网络回调，深调用链超过栈容量。
栈溢出直接触发 ESP32-S3 整机软复位，表现为周期性重复启动序列。

### 修复

- 栈从 2 560 B 提升到 5 120 B；
- 连接成功后打印栈余量：

```cpp
ESP_LOGI(TAG, "巴法云已连接（任务栈余量 %u B）",
         (unsigned)uxTaskGetStackHighWaterMark(nullptr));
```

- 保留内部 RAM 栈，不迁 PSRAM：该任务启动时读写 NVS，PSRAM 栈在 flash 写期间会崩。

### 验证

- 连续冷启动无 `stack overflow`；
- 连接后栈余量 > 1 KB；
- 云上报、OTA、AFE 同时运行无重启。

### 经验

栈溢出日志会直接给出任务名，先查该任务的最深调用链，不要先怀疑 heap。
一次加够（2.5 KB）比反复 512 B 试探更省时间；加完必须打印高水位。

---

## 5. WebSocket 握手 10 s 超时

### 现象

TLS 证书验证成功，但 WebSocket 握手超时：

```
WebSocket handshake timeout
```

### 根因

`tcp_->Connect()` 成功后，`EspSsl` 的 `ssl_receive` 任务已经开始收数据；旧代码在
`tcp_->Send(request)` 之后才注册 `OnStream()` / `OnDisconnected()`。如果服务端的
101 响应在注册前到达，`ReceiveTask` 会因为 `stream_callback_` 为空而丢弃数据，
之后只能等 10 s 超时。

另外，握手期服务端主动断开时，旧 `OnDisconnected` 不置失败位，只能白等 10 s，
日志还会误报 timeout。

### 修复

`managed_components/78__esp-ml307/src/web_socket.cc`：

1. 把 `OnStream()` / `OnDisconnected()` 注册与 `xEventGroupClearBits()` 移到
   `tcp_->Send(request)` **之前**；
2. `OnDisconnected` 在 `!handshake_completed_` 时置 `HANDSHAKE_FAILED_BIT`。

### 验证

- 握手成功日志稳定出现；
- 服务端主动断开时快速返回失败，不再固定等 10 s；
- 连续建链/断链 10 次无异常。

### 经验

「先注册回调，再发请求」是异步 I/O 的通用纪律。任何可能在注册前到达的响应，
都会造成随机超时，且很难从日志直接看出。

---

## 6. 触摸 IC 进入 Monitor 模式导致崩溃重启

### 现象

屏幕周期性闪烁（实际是崩溃重启循环），串口有 `Guru Meditation` / `Rebooting`；
触摸完全无响应。按 SW1 或断电后恢复。

### 根因链

```
无触摸满 30 s（FT6336U 0x87 默认 0x1E）
  → 芯片自动进入 Monitor，I2C 串口整体关闭
  → 驱动 init 读回全 0x00（正常 Vendor ID 应为 0x11）→ 失败
  → 驱动跳过错写句柄、释放自己的句柄
  → 板级代码使用未初始化的局部句柄，且未检查返回值
  → 野指针交给 lvgl_port_add_touch
  → LoadProhibited → 重启 → 循环
```

### 修复

1. 驱动初始化写入 `0x86 = 0x00`，禁止自动进入 Monitor：

```c
#define FT62XX_REG_MONITORCTRL 0x86
ret |= touch_ft6x36_i2c_write(tp, FT62XX_REG_MONITORCTRL, 0x00);
```

2. 回读并打印 `Monitor ctrl (0x86) = 0x00 (expect 0x00)`；
3. 板级 `tp` 句柄初始化为 `nullptr`，失败直接返回，不注册触摸；
4. 触摸中断改为 GPIO10 事件驱动，替代 33 ms 轮询。

### 验证

- 日志出现 `Monitor ctrl (0x86) = 0x00`；
- 静置数分钟后触摸仍正常；
- 软复位后不再出现触摸读 ID 失败导致的崩溃循环。

### 经验

I2C 从机可能「自己睡着」，驱动初始化必须考虑芯片状态机；空/野句柄绝不能交给上层
框架，`assert(handle)` 挡不住栈上的垃圾值。

---

## 7. 全屏滚动卡顿：SPI 带宽瓶颈

### 现象

车载页面滑动/刷新卡顿，FPS 只有 26–30；调任务优先级、缓冲行数、刷新周期都没用。

### 根因

- 480×320×2 = **307 200 B/帧**；
- SPI 80 MHz 纯传输约 **30.7 ms**；
- 实测帧时间约 40 ms，传输占约 77%，渲染仅约 9 ms。

瓶颈是 SPI 带宽，不是 CPU 或 LVGL 渲染。

### 修复

1. 把车载界面拆成**数据页 + 摄像头页**两个独立 screen；
2. 两页都 `lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE)`；
3. 每帧脏区从 307 KB 降到几 KB；
4. 数据页 500 ms 刷新 + 文本去重；
5. 摄像头预览只在摄像头页取帧，10 FPS。

### 验证

- 切页/刷新无明显卡顿；
- 数据页数值更新正常；
- 摄像头页 `Preview stats` 有稳定 fps。

### 经验

无 TE 引脚的 SPI 屏，帧率上限由「每帧要传的字节数」决定。缩小脏区比调优先级有效得多；
布局改造就是性能改造。

---

## 8. 触摸间歇性失效：内部 RAM 低水位

### 现象

触摸偶尔失灵，多次点击才响应一次；内部 RAM 越低越明显。

### 根因

M4 接入 esp-dl 后，内部 RAM 被压到全周期最低仅约 23 B。触摸 I2C 读取需要内部堆
分配事务缓冲，分配失败时本次点击被丢弃；表现为「多次点击才响应」。

### 修复

- LVGL 绘制缓冲从 30 行降到 15 行，省 **28.8 KB** 内部 RAM；
- 触摸改为 GPIO10 中断驱动，事件到达即读，不再依赖 33 ms 轮询；
- 降低预览/检测对 CPU0 的占用，避免 I2C 事务被调度挤占。

### 验证

- 连续点击 20 次无漏点；
- `internal_heap` 稳态约 32–33 KB，最低 9–17 KB；
- 触摸日志无 I2C 失败。

### 经验

「触摸不灵」不一定是触摸 IC 或驱动问题，可能是系统内部 RAM 被压干导致 I2C 事务
分配失败。先看 `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)`。

---

## 9. AFE 环形缓冲溢出刷屏与任务创建失败

### 现象

串口持续刷 AFE 环形缓冲溢出警告；严重时 `audio_detection` 任务创建失败，
唤醒词检测不可用。

### 根因

1. 编码队列深度只有 2，`push` 会阻塞；生产端 `audio_communication` 优先级 3
   抢占消费端 `opus_codec` 优先级 2，生产端卡在 push → 停止 fetch AFE →
   环形缓冲只进不出。
2. 同步串口日志抢占 CPU，形成「溢出→刷日志→更卡→更溢出」的恶性循环。
3. 启动窗口内部 RAM 碎片导致 `audio_detection` 的 4 KB 内部栈创建失败。

### 修复

- `opus_codec` 优先级从 2 提到 4，保证消费端 ≥ 生产端；
- `audio_detection` 内部栈创建失败时，回退到 PSRAM 栈：

```cpp
ret = xTaskCreatePinnedToCoreWithCaps(detection_task, "audio_detection",
                                      4096, this, 3, nullptr, 0,
                                      MALLOC_CAP_SPIRAM);
```

- 该任务只做 fetch/存音频，不写 flash，PSRAM 栈安全；
- AFE 内部处理任务优先级保持 1，不上调（上调到 5 会与 LVGL 抢 CPU1，导致黑屏重启）。

### 验证

- 唤醒→对话→TTS→Idle 连续 5 轮无溢出刷屏；
- 二次唤醒正常；
- 串口无 `audio_detection` 创建失败。

### 经验

 producer/consumer 队列满时，消费端优先级必须不低于生产端；否则生产端阻塞会反压
整个实时链路。日志本身也会成为实时性负担，必要时降级日志级别。

---

## 10. 巴法云 MQTT 连接风暴

### 现象

巴法云连上几分钟后被 RST，此后一直起不来；日志显示反复连接。

### 根因

本工程 `Mqtt::Connect()` 会先 `Disconnect()` 销毁客户端再重建。若每 60 s 调一次，
等于用同一个 `clientId`（用户私钥）反复向 broker 发起连接，被判定为连接风暴并拒绝。

### 修复

- 保持客户端实例单次初始化，依赖 `esp-mqtt` 内置自动重连；
- 只在「从未连上过」或「已连过但连续断线 > 180 s」时主动 `Connect()`；
- 连接建立后订阅 4 个主题；
- 断线期间告警入有界队列（32 条），重连后补发。

### 验证

- 巴法云在线 30 分钟无 RST；
- 断网/恢复后自动重连；
- 告警补发顺序正确。

### 经验

MQTT 客户端不要频繁销毁重建；重连策略要尊重 broker 的连接频率限制。
用 `clientId` 做鉴权时，重复建链的代价更高。

---

## 11. 软复位后触摸失效 / 屏幕全黑

### 现象

烧录或软复位后屏幕全黑，串口停在 PCA9557 初始化超时；重新插拔屏幕排线 + 整板断电
后恢复。

### 根因

14 针 LCD FPC 排线接触不良，导致共享 I2C1 总线被拉死；PCA9557 初始化超时触发
`ESP_ERROR_CHECK` abort，形成软复位死循环。软复位不会给触摸 IC 断电，触摸可能停留在
Monitor 模式，进一步加剧。

### 修复

- PCA9557 初始化改为失败降级，不用 `ESP_ERROR_CHECK` 卡死；
- 触摸驱动写 `0x86=0x00`，防止自动 Monitor；
- 触摸初始化失败直接跳过注册；
- I2C 设备探测单次失败即退出，不死循环。

### 验证

- 软复位 10 次无黑屏；
- 排线重新插好后 I2C 扫描正常；
- PCA9557 超时只打印错误，系统继续启动。

### 经验

共享 I2C 总线上任何一个器件把总线拉死，都会表现为「另一个器件初始化失败」。
硬件接触问题优先排查排线/供电，软件侧要做超时和降级。

---

## 12. 浮点格式化失效（nano newlib）

### 现象

`printf("%.1f", value)` 输出空/错误；`std::to_string(float)` 不可靠；
云端 JSON 出现 `null` 或异常字符。

### 根因

ESP32-S3 的 newlib 为 nano 格式，默认不支持浮点格式化。

### 修复

统一用整数运算拼一位小数：

```cpp
void FormatFixed1(char* out, size_t out_size, float value) {
    int v10 = (int)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    if (v10 < 0) {
        snprintf(out, out_size, "-%d.%d", (-v10) / 10, (-v10) % 10);
    } else {
        snprintf(out, out_size, "%d.%d", v10 / 10, v10 % 10);
    }
}
```

- `car_cloud.cc`、`car_monitor_helper.cc`、`car_ui.cc` 全部使用该方式；
- 日志中的浮点也改用整数格式（如 `fps_x10`、`score_x100`）。

### 验证

- 云端 JSON 数值正常；
- UI 温度/湿度显示正常；
- 日志无 `%f` 相关异常。

### 经验

嵌入式工具链的 libc 配置会直接影响业务代码；跨平台/跨 SDK 时先验证浮点格式化。

---

## 13. 模型/摄像头/云任务互相抢内部 RAM

### 现象

同时开启人员检测、摄像头预览、巴法云后，随机出现任务创建失败、OTA 失败、触摸失灵。

### 根因

内部 RAM 只有约 195 KB 可动态分配，多个任务同时申请连续块时互相挤占：

- esp-dl 模型加载瞬时 21 KB；
- 摄像头 DMA 行缓冲 7.7 KB；
- MQTT 客户端数 KB；
- TLS 16 KB 连续块；
- 任务栈 4–5 KB/个。

### 修复

- 启动顺序按内存峰值排序（网络→模型→IMU→云延迟）；
- 云上报延迟 15 s；
- 预览/检测/云全部钉 CPU0，音频/LVGL 在 CPU1；
- 会话期检测降频 500→1000 ms；
- LVGL 缓冲减半；
- 失败只降级，不重启。

### 验证

- 连续冷启动 5 次无失败；
- OTA/云/检测/语音同时运行 30 分钟无重启；
- `internal_heap` 最低水位 ≥ 9 KB。

### 经验

多任务系统的稳定性不是「每个任务都能跑」，而是「最坏时刻还能分配出关键连续块」。
启动顺序、延迟启动、任务钉核都是内存治理手段。

---

## 14. managed_components 本地补丁记录

`managed_components/` 被 `.gitignore` 忽略，删除目录重新拉取依赖会还原上游代码，
以下补丁会丢失，必须记录：

| 日期 | 文件 | 补丁 | 原因 |
| --- | --- | --- | --- |
| 2026-10-21 | `78__esp-ml307/src/web_socket.cc` | `OnStream()` / `OnDisconnected()` 注册与 `xEventGroupClearBits()` 移到 `tcp_->Send(request)` 之前 | 服务端 101 可能在回调注册前到达，`ReceiveTask` 因 `stream_callback_` 为空丢数据，表现为 10 s 握手超时 |
| 2026-10-22 | 同上 | `OnDisconnected` 在 `!handshake_completed_` 时置 `HANDSHAKE_FAILED_BIT` | 握手期服务端主动断开时旧代码不置失败位，只能白等 10 s |

---

## 15. 问题定位方法沉淀

### 15.1 先分清「崩溃」与「卡顿」

- 屏幕闪烁先看串口有没有 `Guru Meditation` / `Rebooting`，不要先怀疑 LVGL 刷新；
- 卡顿先量「每帧字节数 / 传输时间 / 渲染时间」，不要先调优先级；
- 触摸不灵先看内部 RAM 最低水位，再看 I2C 波形/中断。

### 15.2 用数据而不是感觉

| 指标 | 接口 |
| --- | --- |
| 内部 RAM 当前/最低 | `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` / `heap_caps_get_minimum_free_size()` |
| PSRAM 当前/最低 | 同上，`MALLOC_CAP_SPIRAM` |
| 任务栈余量 | `uxTaskGetStackHighWaterMark(nullptr)` |
| 预览帧率/耗时 | `CarUIHelper` 每秒统计 `fps/get/lock+draw/max_frame` |
| 推理耗时 | `PersonDetect` 每次检测打印 `耗时=xx ms` |
| 云端负载长度 | `CarCloud` 首次上报打印 `env=xx B, state=xx B` |

### 15.3 二分开关

代码里保留了几个诊断开关，用于快速定位：

| 开关 | 位置 | 作用 |
| --- | --- | --- |
| `kEnableImuMotion` | `application.cc` | 关闭 IMU 任务，二分触摸/唤醒问题 |
| `kEnableCloudUpload` | `application.cc` | 关闭云上报，二分内存/OTA 问题 |
| `kEnablePersonDetect` | `car_person_detect.cc` | 关闭推理，二分 CPU/PSRAM 抢占 |
| `kDumpModelMemory` | `car_person_detect.cc` | 打印 esp-dl 逐层内存 |

### 15.4 修改纪律

1. 一次只改一个变量，改完记录 `minimal sram` / 栈余量 / 帧率；
2. 任何 managed_components 补丁必须写入本文档；
3. 失败路径必须归还资源（fb、队列、句柄）；
4. 初始化失败一律降级，不阻塞启动；
5. 跨任务操作统一走队列/事件/Schedule，不在回调里直接操作硬件或 LVGL。

---

## 16. 结论

本项目遇到的问题可以归为五类：

1. **协议时序**：goodbye、WebSocket 握手回调、MQTT 重连；
2. **并发资源**：摄像头单帧缓冲、LVGL 锁、队列满死锁；
3. **内存**：内部 RAM 峰值/碎片、栈溢出、PSRAM 栈限制；
4. **实时性**：音频优先级、CPU 钉核、看门狗；
5. **带宽**：SPI 全屏传输、脏区、预览帧率。

每一类都遵循同一个原则：**把不确定性（时序/内存/调度）显式化，变成可观测的指标和
可降级的分支**，而不是靠加大栈、提高优先级、反复重启来掩盖。
