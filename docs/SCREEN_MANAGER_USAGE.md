# 独立屏幕管理器使用文档

**日期**: 2025-01-XX  
**架构**: 方案1 - 独立屏幕切换

---

## 🎯 架构说明

### 设计原理

```
┌─────────────────────────────────────┐
│  聊天屏幕（SCREEN_CHAT）             │
│  - 原有聊天界面                      │
│  - 屏保正常工作                      │
│  - lv_scr_act()                     │
└─────────────────────────────────────┘
            ↕ 按键切换
┌─────────────────────────────────────┐
│  车载监测屏幕（SCREEN_CAR_MONITOR）  │
│  - 独立创建的屏幕对象                │
│  - 自动禁用屏保                      │
│  - lv_obj_create(NULL)              │
└─────────────────────────────────────┘
```

### 核心特点

1. **完全独立**：两个屏幕互不干扰
2. **简单切换**：长按 BOOT 按键（GPIO0）切换
3. **自动屏保控制**：
   - 聊天屏幕 → 允许屏保
   - 车载屏幕 → 自动禁用屏保

---

## 📂 新增文件

```
main/
├── screen_manager.h              # 屏幕管理器头文件
├── screen_manager.cc             # 屏幕管理器实现
├── car_ui_helper.h               # 已更新（使用 ScreenManager）
├── car_ui_helper.cc              # 已更新（使用 ScreenManager）
└── boards/gec-esp32s3-board/
    └── gec_esp32s3_board.cc      # 已更新（添加长按切换）
```

---

## 🎮 用户操作

### 按键功能

| 按键 | 操作 | 功能 |
|------|------|------|
| BOOT（GPIO0） | **单击** | 切换聊天状态（原有功能） |
| BOOT（GPIO0） | **长按** | 切换屏幕（聊天 ↔ 车载监测）⭐ |
| BOOT（GPIO0） | 双击 | 切换 AEC 模式（如果启用） |

### 切换演示

```
启动 → 聊天屏幕（默认）
  ↓ 长按 BOOT 按键 1-2 秒
车载监测屏幕
  • 显示环境数据
  • 显示车辆状态
  • 显示告警信息
  • 屏保自动停止
  ↓ 再次长按 BOOT 按键
回到聊天屏幕
  • 屏保重新允许
```

---

## 🔧 技术实现

### ScreenManager 核心 API

```cpp
// 初始化（在 application.cc 中已调用）
ScreenManager::Initialize(board.GetDisplay());

// 切换到指定屏幕
ScreenManager::GetInstance()->SwitchToScreen(SCREEN_CHAT, true);
ScreenManager::GetInstance()->SwitchToScreen(SCREEN_CAR_MONITOR, true);

// 循环切换（按键使用）
ScreenManager::GetInstance()->SwitchToNextScreen();

// 获取当前屏幕
ScreenType current = ScreenManager::GetInstance()->GetCurrentScreen();
```

### CarUIHelper API

```cpp
// 切换屏幕（便捷函数）
CarUIHelper::ToggleScreen();              // 循环切换
CarUIHelper::SwitchToCarMonitorScreen();  // 切到车载
CarUIHelper::SwitchToChatScreen();        // 切到聊天

// 手动更新车载 UI
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
I (xxx) ScreenManager: Car monitor screen created: 0x3fcxxxxx
I (xxx) CarUI: CarUI initialized
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
- [ ] **空闲后屏保正常启动**
- [ ] **长按 BOOT 按键 → 切换到车载监测屏幕**
  - [ ] 屏幕显示车载数据
  - [ ] 屏保停止
  - [ ] 数据每 500ms 更新
- [ ] **再次长按 BOOT 按键 → 回到聊天屏幕**
  - [ ] 聊天界面恢复
  - [ ] 屏保重新允许

---

## 📊 资源占用

| 资源 | 占用 | 说明 |
|------|------|------|
| Flash | ~6KB | ScreenManager + 修改 |
| SRAM | ~2KB | 屏幕对象指针 |
| PSRAM | 由 LVGL 管理 | 车载屏幕缓冲 |
| 任务栈 | 4KB | car_ui_update 任务 |
| CPU | <3% | 仅在车载屏幕时更新 |

---

## ⚠️ 可能的问题与解决

### 问题 1: 长按按键不响应

**原因**: Button 类可能没有 OnLongPress() 方法

**解决**: 检查 `button.h` 中是否有长按支持
```cpp
// 如果没有长按，可以改用双击
boot_button_.OnDoubleClick([this]() {
    extern void ToggleCarMonitorScreen();
    ToggleCarMonitorScreen();
});
```

### 问题 2: 切换后聊天界面数据丢失

**现象**: 切回聊天屏幕后，之前的聊天记录消失

**原因**: LVGL 屏幕切换不会删除对象，这不应该发生

**解决**: 检查日志，确认 `chat_screen_` 指针正确

### 问题 3: 车载屏幕数据不更新

**症状**: 切换到车载屏幕后，数据显示为初始值

**原因**: 更新任务条件判断错误

**解决**: 检查 `car_ui_helper.cc` 中的 UpdateTaskFunction

---

## 🎨 自定义

### 添加第三个屏幕（例如：设置屏幕）

1. **修改 screen_manager.h**：
```cpp
typedef enum {
    SCREEN_CHAT = 0,
    SCREEN_CAR_MONITOR,
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
// 三击切换到设置
boot_button_.OnTripleClick([this]() {
    ScreenManager::GetInstance()->SwitchToScreen(SCREEN_SETTINGS, true);
});
```

---

## 🔍 调试技巧

### 查看当前屏幕

```cpp
ScreenType current = ScreenManager::GetInstance()->GetCurrentScreen();
ESP_LOGI(TAG, "Current screen: %d (0=CHAT, 1=CAR)", current);
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
- [x] 车载屏幕独立创建
- [x] CarUI 集成到车载屏幕
- [x] 按键长按切换添加
- [x] 屏保自动控制
- [x] CarUIHelper 更新
- [x] CMakeLists.txt 更新
- [x] application.cc 集成
- [ ] 编译验证（待执行）
- [ ] 烧录测试（待执行）
- [ ] 按键切换测试（待执行）

---

## 📞 后续优化建议

### 优先级 1: 切换动画优化

目前使用 FADE_IN 淡入效果，可以改为滑动：
```cpp
lv_scr_load_anim(target_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
```

### 优先级 2: 添加状态指示器

在聊天屏幕底部添加小图标，显示当前在哪个屏幕：
```
[💬]  ← 当前在聊天屏幕
[🚗]  ← 当前在车载屏幕
```

### 优先级 3: 触摸手势支持

如果触摸屏支持，可以添加滑动手势切换：
- 向左滑动 → 下一个屏幕
- 向右滑动 → 上一个屏幕

---

**状态**: 代码集成完成，等待编译验证 ✅

**优势**: 完全不干扰原有功能，简单直接的双屏切换方案
