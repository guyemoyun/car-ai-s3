# ESP32-S3 内部 RAM 可改进方案（暂不实施）

> 状态：方案草案，仅记录思路，本文档对应的代码改动**现在不做**。  
> 日期：2026-10-21  
> 关联：`car_cloud` 栈溢出修复（2560 B → 5120 B）已实施，不属于本方案范围。

## 0. 结论先行

- 当前内部 RAM 的瓶颈不是"总量 512KB 不够"，而是**开机后真正可动态分配的只有约 259KB**，其中还有 64KB 被划为 DMA 专用池。
- 空闲稳态估计 **30–60KB**，小智语音会话期间最低可能到 **10–25KB**。
- 比"继续加大任务栈"更有效的方向是：
  1. 把低频、不碰 flash 的任务栈迁到 PSRAM（约 12KB）；
  2. 评估 WiFi/LWIP 缓冲优先走 PSRAM（可能 10–40KB）；
  3. 用实测栈水位回收 `opus_codec` 等大栈的冗余（可能 8–16KB）。
- 所有方案都必须以 `minimal sram` 和 `uxTaskGetStackHighWaterMark()` 实测为依据，不凭感觉改。

## 1. 当前内存账本（基线）

### 1.1 开机可用量

| 项目 | 数值 | 来源 |
|---|---|---|
| 内部动态堆（3 块合计） | 206 + 21 + 32 ≈ **259KB** | 启动日志 `heap_init` |
| 预留 DMA/内部专用池 | **64KB** | `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536` |
| 普通内部堆 | ≈ **195KB** | 259 − 64 |
| 静态区 + IDF/WiFi 固定占用 | ≈ **253KB** | 512 − 259 |
| PSRAM | **8MB** Octal 80MHz | 启动日志 |
| Flash | **16MB** QIO | `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` |

### 1.2 主要内部 RAM 占用（估算）

| 占用项 | 约占用 | 说明 |
|---|---|---|
| 任务栈合计 | **~90KB** | 含系统任务；`opus_codec` 26KB 最大 |
| LVGL 绘制缓冲 | **28.8KB** | 480×15×2B×双缓冲，必须内部 DMA |
| I2S 音频 DMA | **~17KB** | RX 4ch + TX 2ch，6×240 帧 |
| 摄像头 DMA 行缓冲 | **~7.7KB** | 帧缓冲在 PSRAM |
| esp-dl 张量池 | **~7.7KB** | 模型主体在 PSRAM |
| WiFi/LWIP/LVGL 小对象 | **40–80KB** | 运行时浮动，部分走 64KB DMA 池 |
| 当前空闲（稳态） | **30–60KB** | 估算，需实测 |
| 会话期最低 | **10–25KB** | TLS 16K in + 4K out + AFE/Opus |

### 1.3 当前 PSRAM 使用情况

| 用途 | 大小 | 位置 |
|---|---|---|
| LVGL 图像缓存 | 2MB | `lcd_display.cc` |
| 摄像头帧缓冲（cam_hal） | 2 × 153600B | `cam_hal` |
| CarUI 预览双缓冲 | 2 × 153600B | `car_ui_helper.cc` |
| 人物检测帧缓冲 | 320×240 级别 | `car_person_detect.cc` |
| esp-dl 模型/张量 | 主体 | esp-dl 默认分配 |
| `encode_wake_word` 任务栈 | 28KB | `afe_wake_word.cc` / `custom_wake_word.cc` |

### 1.4 已启用的相关配置

| 配置 | 状态 |
|---|---|
| `CONFIG_SPIRAM_USE_MALLOC` | `y` |
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` | `512`（<512B 分配强制内部） |
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` | `65536` |
| `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` | `y`（允许 PSRAM 任务栈） |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | 未开启 |
| `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` | 未开启 |

## 2. 改进项总表

| 优先级 | 编号 | 方案 | 预计收益 | 风险 | 是否现在做 |
|---|---|---|---|---|---|
| P0 | M0 | 先补齐水位观测，建立基线 | 0KB，但决定后续是否值得改 | 无 | 建议先做观测，不改代码 |
| P1 | M1 | `cam_preview` / `car_ui_update` / `car_monitor` 栈迁 PSRAM | ~12KB | flash 写期间任务可能崩；PSRAM 访问变慢 | 暂不 |
| P1 | M2 | 开启 `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | 10–40KB（估） | WiFi 稳定性、吞吐、延迟需实测 | 暂不 |
| P2 | M3 | 把 `car_cloud` 的 NVS 操作移出任务后再迁栈 | 5KB | 需调整凭据/boot_id 归属 | 暂不 |
| P2 | M4 | 实测后回收 `opus_codec` 26KB 栈冗余 | 8–16KB（估） | 音频路径回归风险 | 暂不 |
| P2 | M5 | 大 `.bss` 放 PSRAM | 视链接结果 | 启动期/cache 依赖 | 暂不 |

## 3. 详细方案

### M0：先建立水位基线（不改代码）

**目的**：确认"空闲 30–60KB、会话期 10–25KB"是否真实，避免盲改。

**做法**：

1. 烧录当前固件（`car_cloud` 5120B），静置 10 分钟。
2. 串口记录：
   - `Application: [DIAG] ... internal_heap=...`（每 10 秒）
   - `SystemInfo: free sram: X minimal sram: Y`
   - `CarCloud: 巴法云已连接（任务栈余量 N B）`
3. 完整唤醒、说 1 分钟、回到 Idle，再记录一次 `minimal sram`。

**判读标准**：

- `minimal sram` ≥ 20KB：内部 RAM 健康，不需要做 M1–M5。
- 10–20KB：可以做 M1 或 M2，但一次只改一项。
- < 10KB：优先做 M2 或 M4，且暂停新增功能。

### M1：低频任务栈迁 PSRAM（约 12KB）

**候选**（栈均 4096B）：

| 任务 | 位置 | 迁栈可行性 |
|---|---|---|
| `cam_preview` | `car_ui_helper.cc` | 高：只取帧+拷贝指针，不写 flash |
| `car_ui_update` | `car_ui_helper.cc` | 高：周期刷 UI，不写 flash |
| `car_monitor` | `components/car_monitor/car_monitor.cc` | 高：周期读传感器 |
| `person_detect` | `car_person_detect.cc` | 中：CPU 密集，PSRAM 栈会增加抖动 |

**实现方式**：

```c
xTaskCreatePinnedToCoreWithCaps(TaskFunc, "cam_preview", 4096, arg,
                                prio, &handle, 0, MALLOC_CAP_SPIRAM);
```

前置条件：

- 已开启 `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y`（当前满足）。
- TCB 必须留在内部 RAM；只有栈放 PSRAM。
- 这些任务内部不得调用 `nvs_commit`、OTA、文件写入等会关 cache 的 flash 操作。
- OTA/NVS 写入期间建议暂停这些任务。

**验证**：

- 连续运行 24 小时无重启。
- 触发一次 NVS 写入与一次 OTA 检查，观察是否崩溃。
- 摄像头预览、UI 刷新、传感器读数无异常。
- `internal_heap` 上升约 12KB，`minimal sram` 同步改善。

**风险与回滚**：任一项任务在 flash 操作期间崩溃，立即改回 `xTaskCreatePinnedToCore`。

### M2：WiFi/LWIP 缓冲优先 PSRAM（收益最大，风险中等）

**做法**：在 `sdkconfig.defaults` 增加：

```ini
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
```

**原理**：WiFi/LWIP 的大块收发缓冲优先分配到 PSRAM，失败再回落内部 RAM；DMA 描述符仍在内部。

**收益**：可能释放 10–40KB 内部 RAM，具体看 WiFi 负载。

**风险**：

- PSRAM 访问延迟和 cache 抖动可能影响 WiFi 吞吐/时延。
- 与 TLS、音频并发时需重点观察断流、重连、丢包。

**验证**：

- 连续 ping 30 分钟，观察丢包与 RTT。
- 小智连续对话 10 轮，无断连、无卡顿。
- 巴法云 MQTT 保持在线 1 小时。
- 若不稳，删除配置回滚并全量重编译。

### M3：`car_cloud` 的 NVS 操作移出任务（5KB）

**问题**：`car_cloud` 启动时读写 NVS `boot_id`，属于 flash 写。只要它在 PSRAM 栈上执行 `nvs_commit`，cache 关闭时必然崩溃。

**做法**：

1. 把 `boot_id` 的读取与自增写入放到 `Application::Start()` 或 `Board` 初始化阶段（内部栈任务）。
2. 把结果通过参数或全局快照传给 `CarCloud::Start()`。
3. 之后再把 `car_cloud` 栈迁到 PSRAM。

**收益**：5KB 内部 RAM，并为后续更多任务迁栈扫清障碍。

**验证**：同 M1，并额外确认断电重启后 `boot_id` 正常自增。

### M4：`opus_codec` 26KB 栈按实测回收（8–16KB）

**问题**：`opus_codec` 任务栈 2048×13 = 26KB，是最大的内部 RAM 任务栈，但可能明显过量。

**做法**：

1. 在 `OpusCodecTask()` 主循环里周期性打印 `uxTaskGetStackHighWaterMark(nullptr)`。
2. 完整跑一轮：唤醒、对话、播放、回到 Idle。
3. 以历史最低余量为准，保留 ≥1KB 安全余量后下调栈。
4. 每轮只减 2KB，并回归音频。

**风险**：栈过小会在特定 Opus 帧或异常路径下溢出，必须用最低水位而非瞬时值判断。

### M5：大 `.bss` 迁 PSRAM（可选）

**做法**：

```ini
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y
```

再用 `EXT_RAM_BSS_ATTR` 标记确定的大数组。

**注意**：只能搬"启动后不再依赖 cache 关闭窗口"的数据；DMA 缓冲、ISR 数据、频繁访问的小结构不要搬。收益需看 `idf.py size` 结果，优先级最低。

## 4. 明确不做的优化

| 不做项 | 原因 |
|---|---|
| 再把 LVGL 绘制缓冲减半 | 已从 30 行降到 15 行；再降会让预览细横线明显恶化 |
| 把 `esp_lcd` 推屏缓冲放 PSRAM | 该路径默认不设置 `SPI_TRANS_DMA_USE_PSRAM`，需要绕开 esp_lcd 自建通路 |
| 把音频输入/输出、AFE、LVGL、主事件循环栈迁 PSRAM | 实时性高或会碰 NVS/OTA/flash，风险大于收益 |
| 把 WiFi/LWIP 的 DMA 描述符放 PSRAM | DMA 描述符必须内部 RAM |
| 为了省内存关闭云上报 | 云上报本身只占约 5KB 栈 + 少量 MQTT 结构，不是主要瓶颈 |

## 5. 验证清单（每项改动后都执行）

- [ ] 冷启动 3 次，无 `stack overflow`、无 `abort`、无重启循环
- [ ] `minimal sram` 记录并对比改动前
- [ ] 触摸连续点击 20 次，无漏点
- [ ] 摄像头预览 10 分钟，无花屏/卡死
- [ ] 小智唤醒→对话→播放→回 Idle 连续 5 轮
- [ ] 巴法云 MQTT 在线 30 分钟，env/state 正常上报
- [ ] 触发一次 OTA 版本检查与一次 NVS 写入
- [ ] 断网/恢复各 1 次，WiFi 能自动重连

## 6. 回滚策略

- 每项改动单独提交，便于 `git revert`。
- PSRAM 栈迁移崩溃时，先改回内部栈，再排查是否由 flash 操作窗口引起。
- M2 导致 WiFi 不稳时，删除 `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` 并全量重编译（不要只改 sdkconfig 缓存）。

## 7. 关键代码位置

| 内容 | 文件 |
|---|---|
| `car_cloud` 任务与栈 | `main/car_cloud.cc` |
| 任务栈定义 | `main/application.cc`、`main/audio/audio_service.cc` |
| LVGL 绘制缓冲 | `main/display/lcd_display.cc` |
| PSRAM 栈示例 | `main/audio/wake_words/afe_wake_word.cc` |
| 堆水位日志 | `main/application.cc`、`main/system_info.cc` |
| PSRAM 预览缓冲 | `main/car_ui_helper.cc` |
| 人物检测缓冲 | `main/car_person_detect.cc` |
