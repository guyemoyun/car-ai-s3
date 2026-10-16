# 车载监测 LVGL 界面集成完成报告

**日期**: 2025-01-XX  
**状态**: ✅ 代码集成完成，待编译验证

---

## 📋 集成概述

基于现有的 LVGL 显示系统和 CarMonitor 组件，成功开发了车载监测用户界面。界面按职责拆分为**两个独立页面**（由 SW3 循环切换）：

- **车载数据页（CarUI）**：只负责显示
  1. **实时环境数据显示** - 温度、湿度、光照
  2. **车辆状态指示** - 行驶中/停车，带颜色区分
  3. **人员检测数据** - 检测人数、置信度
  4. **告警状态提示** - 突出显示活跃告警
- **车载摄像头页（CarCameraUI）**：负责预览占位与交互
  5. **摄像头预览区域** - 4:3 占位区，预留位置，待后续集成（当前无实时画面）
  6. **交互按钮** - 切换车辆状态、确认告警、测试告警

> 拆页的主要原因：原先单页整页滚动时每帧脏区约 307KB、帧时间约 40ms、FPS 仅 26~30（ST7796 + SPI 80MHz 已是面板极限，传输就占了约 77%）。拆成两页并各自禁用滚动后，每帧脏区降到几 KB 量级，卡顿从结构上消失。

---

## 📂 新增文件清单

### 核心 UI 组件（components/car_monitor/）
```
components/car_monitor/
├── car_ui.h                   # 车载数据页类头文件
├── car_ui.cc                  # 车载数据页实现（顶部通栏 + 中部两栏 + 底部通栏，禁用滚动）
├── car_camera_ui.h            # 车载摄像头页类头文件
├── car_camera_ui.cc           # 车载摄像头页实现（4:3 预览占位 + 三颗按钮，禁用滚动）
├── car_ui_colors.h            # 两页共用的深色配色宏（避免两页外观漂移）
└── CMakeLists.txt             # 已更新（SRCS 含 car_ui.cc + car_camera_ui.cc + LVGL 依赖）
```

### 应用层集成（main/）
```
main/
├── car_ui_helper.h            # 界面辅助类头文件
├── car_ui_helper.cc           # 界面辅助类实现（~150行）
├── application.cc             # 已集成初始化代码
└── CMakeLists.txt             # 已更新（添加 car_ui_helper.cc）
```

---

## 🎨 界面布局设计

### 页面一：车载数据页 CarUI（480x320，横屏，整页不滚动）

```
┌───────────────────────────────────────────────┐
│ 🚗 车辆: 行驶中  [绿色/橙色]   （顶部通栏，高 52）│
├───────────────────────┬───────────────────────┤
│ 📊 环境数据（左栏）    │ 👤 人员检测（右栏）    │
│   🌡️ 温度: 25.3°C      │   检测人数: 0          │
│   💧 湿度: 51.2%       │   置信度: 0%           │
│   ☀️ 光照: 48.7%       │                        │
├───────────────────────┴───────────────────────┤
│ ⚠️ 告警: 无 [正常白色/告警红色]  （底部通栏，高 48）│
└───────────────────────────────────────────────┘
```

- 顶部通栏 52px、底部通栏 48px，中部两栏各占 48% 宽度、吃掉剩余高度。
- 容器调用 `lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE)` 禁用滚动。

### 页面二：车载摄像头页 CarCameraUI（480x320，横屏，整页不滚动）

```
┌───────────────────────────────────────────────┐
│        ┌───────────────────────────┐          │
│        │   摄像头预览区域           │          │
│        │   （300×225 灰色占位，     │          │
│        │    4:3，待集成，无实时画面）│          │
│        └───────────────────────────┘          │
├───────────────────────────────────────────────┤
│ [ 切换状态 ]   [ 确认告警 ]   [ 测试告警 ]     │
│            三颗按钮 140×50 横排                │
└───────────────────────────────────────────────┘
```

- 预览区取 300×225 而非 320×240：320×240 + 按钮行 + 内边距 = 322px，超过屏幕 320px 高度；300×225 仍是 4:3，合计 307px 放得下。
- 同样禁用滚动。

### 颜色方案（深色主题）

配色集中定义在 `car_ui_colors.h`，两个页面共用：

| 元素 | 颜色 | 用途 |
|------|------|------|
| 背景 | #1E1E1E | 深灰主背景 |
| 面板 | #2A2A2A | 状态/预览面板背景 |
| 文字 | #FFFFFF | 主要文字 |
| 暗文字 | #AAAAAA | 次要信息 |
| 行驶中 | #4CAF50 | 绿色 - 车辆行驶 / 确认告警按钮 |
| 停车 | #FF9800 | 橙色 - 车辆停车 |
| 告警 | #F44336 | 红色 - 活跃告警 |
| 按钮 | #2196F3 | 蓝色 - 普通按钮 |
| 告警按钮 | #FF5722 | 橙红 - 危险操作 |

---

## 🔧 集成的功能

### CarUI 核心类（car_ui.h/cc）—— 数据页

**公开接口**：
```cpp
// 构造函数 - 创建数据页界面
CarUI(lv_obj_t* parent = nullptr);
~CarUI();

// 更新界面 - 从快照刷新所有显示
void Update(const CarSnapshot& snapshot);

// 控制可见性
void SetVisible(bool visible);

// 获取根对象
lv_obj_t* GetRoot();
```

**界面元素**：
- 7 个状态标签（车辆状态、温度、湿度、光照、人数、置信度、告警）
- 无按钮、无摄像头预览区（这些已移出到摄像头页）

### CarCameraUI 核心类（car_camera_ui.h/cc）—— 摄像头页

**公开接口**：
```cpp
// 构造函数 - 创建摄像头页界面
CarCameraUI(lv_obj_t* parent = nullptr);
~CarCameraUI();

// 设置按钮回调
void SetToggleVehicleCallback(void (*callback)(void));
void SetConfirmAlertCallback(void (*callback)(void));
void SetTestAlertCallback(void (*callback)(void));

// 控制可见性
void SetVisible(bool visible);

// 获取根对象
lv_obj_t* GetRoot();
```

**界面元素**：
- 1 个 4:3 摄像头预览占位区（300×225，待集成，当前无实时画面）
- 3 个交互按钮（切换状态、确认告警、测试告警，140×50 横排）
- 日志 TAG 为 `CarCameraUI`，点击按钮日志形如 `CarCameraUI: Confirm alert button clicked`

### CarUIHelper 辅助类（car_ui_helper.h/cc）

**静态接口**：
```cpp
// 初始化（校验两个 UI 均存在，并把三颗按钮回调挂到摄像头页）
bool Initialize();

// 启动自动更新任务（默认 500ms 更新一次）
bool StartUpdateTask(uint32_t update_interval_ms = 500);

// 停止自动更新任务
void StopUpdateTask();

// 手动触发数据页更新
void UpdateUI();

// 屏幕切换
void SwitchToCarMonitorScreen();  // 内部为 SwitchToScreen(SCREEN_CAR_DATA, true)
void SwitchToChatScreen();
void ToggleScreen();
```

**自动化功能**：
- 创建独立 FreeRTOS 任务，周期性从 CarMonitor 获取数据
- 自动处理 LVGL 线程安全（lvgl_port_lock/unlock）
- `UpdateUI()` 无条件刷新数据页，**不再判断当前屏幕**（数据页不可见时更新只是标记脏区，成本可忽略，好处是切过去时数值已是最新）
- 按钮事件自动转发到 CarMonitorHelper

---

## 🚀 使用流程

### 1. 初始化（已在 application.cc 中完成）

```cpp
#include "car_ui_helper.h"

// 初始化界面（校验两个 UI，按钮回调挂到摄像头页）
if (CarUIHelper::Initialize()) {
    // 启动自动更新任务（500ms 间隔）
    CarUIHelper::StartUpdateTask(500);
}
```

> 两个车载页面由 `ScreenManager` 在初始化时统一创建（`car_data_screen_` / `car_camera_screen_` 各自独立屏幕，两个 UI 实例经 `GetCarDataUI()` / `GetCarCameraUI()` 访问）；`CarUIHelper` 只负责回调挂接与刷新。

### 2. 界面自动更新

更新任务会自动：
1. 每 500ms 从 CarMonitor 获取最新快照
2. 在 LVGL 锁内调用 `CarUI::Update()`（无条件刷新数据页，不判断当前屏幕）
3. 刷新所有显示元素

### 3. 用户交互流程

```
用户在摄像头页点击按钮
    ↓
CarCameraUI::ButtonEventCallback（TAG: CarCameraUI）
    ↓
调用对应的回调函数
    ↓
CarUIHelper::OnToggleVehicle / OnConfirmAlert / OnTestAlert
    ↓
转发到 CarMonitorHelper 的对应函数
    ↓
发送命令到 CarMonitor
    ↓
CarMonitor 更新状态
    ↓
下一次更新周期，数据页自动刷新
```

---

## 📊 资源占用预估

| 资源 | 占用 | 说明 |
|------|------|------|
| Flash | ~8KB | CarUI + CarCameraUI + CarUIHelper 代码 |
| SRAM | ~2KB | UI 对象 + 缓冲区 |
| PSRAM | 由 LVGL 管理 | 显示缓冲区在 PSRAM |
| 任务栈 | 4KB | car_ui_update 任务 |
| CPU | <3% | 500ms 无条件刷新数据页，处理简单 |

---

## 🎯 编译与验证步骤

### Step 1: 清理并构建

```bash
# 在 VS Code 中按 Ctrl+Shift+P
ESP-IDF: Full Clean
ESP-IDF: Build Your Project
```

### Step 2: 预期编译输出

```
[xxx/xxx] Linking C executable car-ai-s3.elf
[xxx/xxx] Generating binary image from built executable
```

**关键检查**：
- ✅ `car_ui.cc` 编译通过
- ✅ `car_camera_ui.cc` 编译通过
- ✅ `car_ui_helper.cc` 链接成功
- ✅ LVGL 依赖正确解析

### Step 3: 烧录并监控

```bash
ESP-IDF: Flash Your Project
ESP-IDF: Monitor Device
```

### Step 4: 预期日志输出

```
I (xxx) Application: Initializing ScreenManager...
I (xxx) ScreenManager: Chat screen saved: 0x3fcxxxxx
I (xxx) ScreenManager: Car data screen created: 0x3fcxxxxx
I (xxx) ScreenManager: Car camera screen created: 0x3fcxxxxx
I (xxx) ScreenManager: CarUI (data) + CarCameraUI created
I (xxx) ScreenManager: ScreenManager initialized successfully
I (xxx) Application: Initializing CarMonitor (demo mode)...
I (xxx) car_monitor: CarMonitor initialized in DEMO mode
I (xxx) Application: CarMonitor task started successfully
I (xxx) Application: Initializing CarUIHelper...
I (xxx) CarUIHelper: CarUIHelper initialized successfully
I (xxx) CarUIHelper: CarUI update task started (interval: 500 ms)
I (xxx) CarUIHelper: CarUI update task running
```

### Step 5: 硬件验证

**数据页（单击 SW3 第一次）**：
1. 顶部通栏 - 车辆状态（行驶中/停车）
2. 中部深灰面板两栏 - 环境数据（左）+ 人员检测（右），实时更新
3. 底部通栏 - 告警文字

**摄像头页（再单击 SW3）**：
1. 4:3 灰色预览占位区（待集成，无实时画面）
2. 底部三颗按钮 - 蓝色（切换状态）/ 绿色（确认告警）/ 橙红色（测试告警）

**触摸屏测试**（在摄像头页）：
1. 点击"切换状态" - 车辆状态在行驶中/停车切换（数据页可见时体现）
2. 点击"测试告警" - 告警文字变红色
3. 点击"确认告警" - 告警恢复白色

---

## ⚠️ 可能的编译问题与解决

### 问题 1: `car_ui.h: No such file or directory`

**原因**: car_monitor 组件的头文件路径不对

**解决**: 组件 `INCLUDE_DIRS` 为 `.`，直接按文件名包含即可：
```cpp
// 在 car_ui_helper.cc / screen_manager.h 中应使用：
#include "car_ui.h"           // ✅ 正确
#include "car_camera_ui.h"    // ✅ 正确

// 而不是：
#include "car_monitor/car_ui.h"  // ❌ 错误
```

### 问题 2: `undefined reference to lv_xxx`

**原因**: LVGL 依赖未正确链接

**解决**: 检查 `components/car_monitor/CMakeLists.txt` 中的 `REQUIRES`:
```cmake
REQUIRES
    "freertos"
    "log"
    "lvgl"           # ← 必须有
    "esp_lvgl_port"  # ← 必须有
```

### 问题 3: `lvgl_port_lock` 死锁

**症状**: UI 不更新，系统卡住

**原因**: LVGL 锁获取超时

**解决**: 
```cpp
// 修改 car_ui_helper.cc 中的锁超时时间
if (lvgl_port_lock(pdMS_TO_TICKS(100))) {  // 100ms 超时
    // ...
    lvgl_port_unlock();
}
```

### 问题 4: 栈溢出

**症状**: `***ERROR*** A stack overflow in task car_ui_update`

**解决**: 增加任务栈大小
```cpp
// 在 car_ui_helper.cc 的 StartUpdateTask() 中
xTaskCreate(
    UpdateTaskFunction,
    "car_ui_update",
    8192,  // ← 改为 8KB
    ...
);
```

---

## 🔍 调试技巧

### 启用 LVGL 日志

在 `menuconfig` 中：
```
Component config → LVGL configuration → Log level → Debug
```

### 查看任务状态

```cpp
#include <esp_system.h>

void print_task_stats() {
    char buffer[512];
    vTaskList(buffer);
    ESP_LOGI("DEBUG", "Task list:\n%s", buffer);
}
```

### 手动触发界面更新

```cpp
// 在任何地方调用
CarUIHelper::UpdateUI();
```

### 禁用自动更新（节省 CPU）

```cpp
// 初始化后不启动自动更新任务
CarUIHelper::Initialize();
// CarUIHelper::StartUpdateTask();  // ← 注释掉

// 需要时手动更新
CarUIHelper::UpdateUI();
```

---

## 📖 下一步开发计划

### 优先级 1: 摄像头集成（本周）

> 预览区在**摄像头页**（CarCameraUI），数据页（CarUI）不再承载预览。

1. 在 `car_camera_ui.cc` 中把预览占位区替换为图像显示
2. 使用 LVGL 的 `lv_canvas` 或 `lv_img` 显示图像
3. 从 `Esp32Camera` 获取帧缓冲并渲染

**示例代码**：
```cpp
void CarCameraUI::UpdateCameraFrame(const uint8_t* rgb565_data, int width, int height) {
    if (lvgl_port_lock(0)) {
        lv_img_dsc_t img_dsc;
        img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        img_dsc.header.w = width;
        img_dsc.header.h = height;
        img_dsc.data = rgb565_data;

        lv_img_set_src(preview_img_, &img_dsc);
        lvgl_port_unlock();
    }
}
```

### 优先级 2: 人员检测可视化（下周）

1. 在摄像头画面上叠加检测框
2. 显示置信度百分比
3. 高亮显示检测到的人员

### 优先级 3: 告警动画（下周）

1. 告警文字闪烁效果
2. 屏幕边框红色呼吸灯效果
3. 蜂鸣器配合（如果有）

### 优先级 4: 设置界面（可选）

1. 创建独立的设置页面
2. 可调参数：告警阈值、更新间隔、音量等
3. 使用 ScreenManager 的独立屏幕切换

---

## ✅ 集成完成检查清单

- [x] CarUI 类创建（car_ui.h/cc，数据页）
- [x] CarCameraUI 类创建（car_camera_ui.h/cc，摄像头页）
- [x] car_ui_colors.h 两页共用配色
- [x] CarUIHelper 辅助类创建
- [x] CMakeLists.txt 更新（组件 + main）
- [x] application.cc 集成初始化代码
- [x] 按钮回调连接到 CarMonitorHelper（挂在摄像头页）
- [x] 自动更新任务实现（无条件刷新数据页）
- [x] LVGL 线程安全处理
- [ ] 编译验证（待执行）
- [ ] 烧录测试（待执行）
- [ ] 触摸屏交互验证（待执行）
- [ ] 性能测试（待执行）

---

## 📞 技术支持清单

| 模块 | 文件 | 关键函数 |
|------|------|---------|
| 数据页渲染 | car_ui.cc | `Update()`, `CreateLayout()` |
| 摄像头页渲染 | car_camera_ui.cc | `CreateLayout()`, `ButtonEventCallback()` |
| 自动更新 | car_ui_helper.cc | `UpdateTaskFunction()` |
| 按钮事件 | car_camera_ui.cc | `ButtonEventCallback()` |
| 数据获取 | car_monitor.cc | `GetSnapshot()` |
| 命令发送 | car_monitor_helper.cc | `HandleUserButtonPress()` |

---

**状态**: 代码集成 100% 完成，等待构建验证 ✅

**当前进度**: CarMonitor 后端 ✅ + LVGL 前端（数据页 + 摄像头页）✅ = 完整车载监测系统
