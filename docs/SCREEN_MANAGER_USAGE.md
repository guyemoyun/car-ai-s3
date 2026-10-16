# 独立屏幕管理器使用文档

**日期**: 2025-01-XX  
**架构**: 方案1 - 独立屏幕切换

---

## 🎯 架构说明

### 设计原理

```
┌─────────────────────────────────────┐
│  聊天屏幕（SCREEN_CHAT = 0）         │
│  - 原有聊天界面                      │
│  - lv_scr_act()                     │
└─────────────────────────────────────┘
            ↕ 单击 SW3
┌─────────────────────────────────────┐
│  车载数据页（SCREEN_CAR_DATA）       │
│  - 独立创建的屏幕对象                │
│  - lv_obj_create(NULL)              │
│  - CarUI：环境数据 / 人员检测 / 告警 │
└─────────────────────────────────────┘
            ↕ 单击 SW3
┌─────────────────────────────────────┐
│  车载摄像头页（SCREEN_CAR_CAMERA）   │
│  - 独立创建的屏幕对象                │
│  - lv_obj_create(NULL)              │
│  - CarCameraUI：预览占位 + 三按钮    │
└─────────────────────────────────────┘
            ↕ 单击 SW3（回到聊天）
```

> 枚举顺序即 SW3 的循环顺序：聊天 → 数据页 → 摄像头页 → 聊天。

### 核心特点

1. **完全独立**：三个屏幕互不干扰
2. **简单切换**：单击 SW3 按键（GPIO11）循环切换

---

## 📂 新增文件

```
main/
├── screen_manager.h              # 屏幕管理器头文件
├── screen_manager.cc             # 屏幕管理器实现
├── car_ui_helper.h               # 已更新（使用 ScreenManager）
├── car_ui_helper.cc              # 已更新（使用 ScreenManager）
└── boards/gec-esp32s3-board/
    └── gec_esp32s3_board.cc      # 已更新（SW3 单击循环切换）

components/car_monitor/
├── car_ui.h / car_ui.cc                # 车载数据页（CarUI）
├── car_camera_ui.h / car_camera_ui.cc  # 车载摄像头页（CarCameraUI）
└── car_ui_colors.h                     # 两页共用的深色配色
```

---

## 🎮 用户操作

### 按键功能

| 按键 | 操作 | 功能 |
|------|------|------|
| BOOT（GPIO0） | **单击** | 切换聊天状态（原有功能） |
| BOOT（GPIO0） | 双击 | 切换 AEC 模式（如果启用） |
| SW3（GPIO11） | **单击** | 循环切换屏幕（聊天 → 数据页 → 摄像头页 → 聊天）⭐ |

### 切换演示

```
启动 → 聊天屏幕（默认）
  ↓ 单击 SW3
车载数据页
  • 车辆状态（顶部通栏）
  • 环境数据 / 人员检测（中部左右两栏）
  • 告警（底部通栏）
  ↓ 再次单击 SW3
车载摄像头页
  • 摄像头预览占位区（待集成）
  • 切换状态 / 确认告警 / 测试告警 三颗按钮
  ↓ 再次单击 SW3
回到聊天屏幕
```

---

## 🔧 技术实现

### ScreenManager 核心 API

```cpp
// 初始化（在 application.cc 中已调用）
ScreenManager::Initialize(board.GetDisplay());

// 切换到指定屏幕
ScreenManager::GetInstance()->SwitchToScreen(SCREEN_CHAT, true);
ScreenManager::GetInstance()->SwitchToScreen(SCREEN_CAR_DATA, true);
ScreenManager::GetInstance()->SwitchToScreen(SCREEN_CAR_CAMERA, true);

// 循环切换（SW3 按键使用）
ScreenManager::GetInstance()->SwitchToNextScreen();

// 获取当前屏幕
ScreenType current = ScreenManager::GetInstance()->GetCurrentScreen();

// 获取两个车载页面 UI 实例
CarUI*        data_ui   = ScreenManager::GetInstance()->GetCarDataUI();    // 数据页
CarCameraUI*  camera_ui = ScreenManager::GetInstance()->GetCarCameraUI();  // 摄像头页
```

### CarUIHelper API

```cpp
// 切换屏幕（便捷函数）
CarUIHelper::ToggleScreen();              // 循环切换下一页
CarUIHelper::SwitchToCarMonitorScreen();  // 切到车载数据页
CarUIHelper::SwitchToChatScreen();        // 切到聊天

// 手动更新车载数据页 UI
CarUIHelper::UpdateUI();
```

---

## 🚀 编译与测试

### Step 1: 编译
```
Ctrl+Shift+P → ESP-IDF: Build Your Project
```

### Step 2: 烧录
```
Ctrl+Shift+P → ESP-IDF: Flash Your Project
```

### Step 3: 预期日志

```
I (xxx) Application: Initializing ScreenManager...
I (xxx) ScreenManager: Chat screen saved: 0x3fcxxxxx
I (xxx) ScreenManager: Car data screen created: 0x3fcxxxxx
I (xxx) ScreenManager: Car camera screen created: 0x3fcxxxxx
I (xxx) ScreenManager: CarUI (data) + CarCameraUI created
I (xxx) ScreenManager: ScreenManager initialized successfully
I (xxx) Application: Initializing CarMonitor (demo mode)...
I (xxx) car_monitor: CarMonitor initialized in DEMO mode
I (xxx) Application: Initializing CarUIHelper...
I (xxx) CarUIHelper: CarUIHelper initialized successfully
I (xxx) CarUIHelper: CarUI update task started (interval: 500 ms)
```

### Step 4: 功能测试

- [ ] **启动后显示聊天界面**（不是白屏）
- [ ] **可以正常与小智对话**
- [ ] **单击 SW3 按键 → 切换到车载数据页**
  - [ ] 屏幕显示车辆状态、环境数据、人员检测、告警
  - [ ] 整页不滚动，数据每 500ms 更新
- [ ] **再次单击 SW3 → 切换到车载摄像头页**
  - [ ] 屏幕显示 4:3 预览占位区 + 三颗按钮
  - [ ] 点击按钮有对应动作（切换状态 / 确认告警 / 测试告警）
- [ ] **再次单击 SW3 → 回到聊天屏幕**
  - [ ] 聊天界面恢复

---

## 📊 资源占用

| 资源 | 占用 | 说明 |
|------|------|------|
| Flash | ~6KB | ScreenManager + 修改 |
| SRAM | ~2KB | 屏幕对象指针 |
| PSRAM | 由 LVGL 管理 | 车载屏幕缓冲 |
| 任务栈 | 4KB | car_ui_update 任务 |
| CPU | <3% | 无条件每 500ms 刷新数据页，非活动屏幕仅标记脏区 |

---

## ⚠️ 可能的问题与解决

### 问题 1: SW3 按键无反应

**原因**: SW3 按键（GPIO11）初始化失败或回调未注册

**解决**: 检查 `gec_esp32s3_board.cc` 构造函数中的 SW3 初始化
```cpp
// SW3 单击切换屏幕
Button* sw3_button = new Button(GPIO_NUM_11, true, 0, 0, false);
sw3_button->OnClick([](){
    CarUIHelper::ToggleScreen();
});
```

### 问题 2: 切换后聊天界面数据丢失

**现象**: 切回聊天屏幕后，之前的聊天记录消失

**原因**: LVGL 屏幕切换不会删除对象，这不应该发生

**解决**: 检查日志，确认 `chat_screen_` 指针正确

### 问题 3: 数据页数据不更新

**症状**: 切换到数据页后，数据显示为初始值

**原因**: 更新任务未启动，或 `GetCarDataUI()` 返回空指针

**解决**: 检查 `car_ui_helper.cc` 中的 UpdateTaskFunction

---

## 🎨 自定义

### 添加第四页（例如：设置页）

1. **修改 screen_manager.h**：
```cpp
typedef enum {
    SCREEN_CHAT = 0,
    SCREEN_CAR_DATA,
    SCREEN_CAR_CAMERA,
    SCREEN_SETTINGS,  // ← 新增
    SCREEN_COUNT
} ScreenType;
```

2. **修改 screen_manager.cc**：
```cpp
// 在 Initialize() 中创建设置屏幕
instance_->settings_screen_ = lv_obj_create(NULL);
// 添加设置界面控件...
```

3. **修改按键逻辑**：
```cpp
// SW3 单击仍按枚举顺序循环；如需直达设置
sw3_button->OnClick([](){
    ScreenManager::GetInstance()->SwitchToScreen(SCREEN_SETTINGS, true);
});
```

---

## 🔍 调试技巧

### 查看当前屏幕

```cpp
ScreenType current = ScreenManager::GetInstance()->GetCurrentScreen();
ESP_LOGI(TAG, "Current screen: %d (0=CHAT, 1=DATA, 2=CAMERA)", current);
```

### 强制切换（用于调试）

在串口监控中，可以调用：
```cpp
// 在代码中添加临时测试按钮
lv_obj_t* test_btn = lv_btn_create(lv_scr_act());
lv_obj_add_event_cb(test_btn, [](lv_event_t* e) {
    CarUIHelper::SwitchToCarMonitorScreen();
}, LV_EVENT_CLICKED, NULL);
```

---

## ✅ 完成检查清单

- [x] ScreenManager 创建
- [x] 聊天屏幕保存
- [x] 车载数据页屏幕独立创建（CarUI）
- [x] 车载摄像头页屏幕独立创建（CarCameraUI）
- [x] 两页共用配色 car_ui_colors.h
- [x] CarUIHelper 更新（按钮回调挂到摄像头页）
- [x] CMakeLists.txt 更新（car_ui.cc + car_camera_ui.cc）
- [x] application.cc 集成
- [ ] 编译验证（待执行）
- [ ] 烧录测试（待执行）
- [ ] SW3 循环切换测试（待执行）

---

## 📞 后续优化建议

### 优先级 1: 添加状态指示器

在聊天屏幕底部添加小图标，显示当前在哪个屏幕：
```
[💬]  ← 当前在聊天屏幕
[📊]  ← 当前在车载数据页
[📷]  ← 当前在车载摄像头页
```

---

**状态**: 代码集成完成，等待编译验证 ✅

**优势**: 完全不干扰原有功能，三页顺序循环、各自整页不滚动，切换结构简单直接
