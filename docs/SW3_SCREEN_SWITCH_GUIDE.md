# SW3 屏幕切换方案完整指南

**日期**: 2025-01-XX  
**方案**: 仅使用 SW3（GPIO11）切换屏幕  
**BOOT 按键**: 保留原有功能，不做修改

---

## 📋 方案说明

### 设计原则

```
BOOT 按键 (GPIO0) - 系统功能
├── 单击 → 切换聊天状态
├── 双击 → 切换 AEC 模式（如果启用）
└── （不涉及屏幕切换）

SW3 按键 (GPIO11) - 用户功能 ⭐
└── 单击 → 切换屏幕（聊天 ↔ 车载监测）
```

### 优势

1. ✅ **职责清晰** - BOOT 管理系统，SW3 管理用户界面
2. ✅ **无功能冲突** - BOOT 保持原样，没有破坏性改动
3. ✅ **简单直接** - 用户一个按键，一个功能
4. ✅ **易于扩展** - 后续可以在 SW3 加长按、双击等扩展功能

---

## 🔧 技术实现

### 修改的文件

**main/boards/gec-esp32s3-board/gec_esp32s3_board.cc**

```cpp
// 第16行：添加头文件
#include "car_ui_helper.h"  // 车载界面辅助类

// 第453-459行：在构造函数中添加 SW3 初始化
// 初始化 SW3 用户按键（GPIO11）用于切换屏幕
Button* sw3_button = new Button(GPIO_NUM_11, true, 0, 0, false);
sw3_button->OnClick([](){
  ESP_LOGI(TAG, "SW3 clicked - switching screen");
  extern void ToggleCarMonitorScreen();
  ToggleCarMonitorScreen();
});
```

### 按键参数说明

```cpp
Button(GPIO_NUM_11, true, 0, 0, false)
       └──┬───┘  └─┬─┘ └┬┘ └┬┘ └──┬──┘
          │       │    │   │     │
          │       │    │   │     └─ 电源节省：false
          │       │    │   └─────── 短按时间：0
          │       │    └─────────── 长按时间：0
          │       └─────────────── 低电平有效（按下为 0）
          └──────────────────────── GPIO11（SW3 按键）
```

---

## 🚀 编译与测试

### Step 1: 重新编译

在 VS Code 中执行：
```
Ctrl+Shift+P → ESP-IDF: Build Your Project
```

**预期**：编译成功，0 errors

### Step 2: 烧录固件

```
Ctrl+Shift+P → ESP-IDF: Flash Your Project
```

### Step 3: 监控日志

```
Ctrl+Shift+P → ESP-IDF: Monitor Device
```

**预期日志输出**：
```
I (xxx) Application: Initializing ScreenManager...
I (xxx) ScreenManager: Chat screen saved: 0x3fcxxxxx
I (xxx) ScreenManager: Car monitor screen created: 0x3fcxxxxx
I (xxx) CarUI: CarUI initialized
I (xxx) ScreenManager: ScreenManager initialized successfully
I (xxx) Application: Initializing CarMonitor (demo mode)...
I (xxx) car_monitor: CarMonitor initialized in DEMO mode
```

### Step 4: 功能测试

1. **启动后** → 显示聊天界面（正常）
2. **单击 SW3 按键** → 切换到车载监测屏幕
   - 预期：淡入淡出过渡动画
   - 预期日志：`I (xxx) gec-esp32s3-board: SW3 clicked - switching screen`
   - 预期：屏幕显示车载监测数据
   - 预期：屏保自动停止
3. **再次单击 SW3** → 回到聊天屏幕
4. **验证数据更新** → 车载数据每 500ms 更新一次

---

## 📊 预期效果

### 聊天屏幕（默认）
```
┌─────────────────────────────┐
│  小智聊天界面                │
│  （正常功能）                │
│  可以与小智对话              │
│  空闲时屏保可启动            │
└─────────────────────────────┘
```

### 车载监测屏幕（单击 SW3）
```
┌─────────────────────────────┐
│  摄像头预览区域              │
│  （灰色占位）                │
├─────────────────────────────┤
│  🚗 车辆: 行驶中 [绿色]      │
│  📊 环境数据                 │
│    🌡️ 温度: 25.3°C          │
│    💧 湿度: 51.2%            │
│    ☀️ 光照: 48.7%           │
│  👤 人员检测                 │
│    检测人数: 0               │
│    置信度: 0%                │
│  ⚠️ 告警: 无                │
└─────────────────────────────┘
```

---

## ⚠️ 故障排查

### 问题 1: SW3 按键无反应

**诊断**：检查日志是否出现 `SW3 clicked`

**原因**：
1. GPIO11 初始化失败
2. Button 库没有正确注册回调

**解决**：
```cpp
// 在构造函数中添加调试日志
ESP_LOGI(TAG, "SW3 button initialized");
```

### 问题 2: 切换屏幕后卡顿

**原因**：ScreenManager 初始化失败

**检查**：
```
I (xxx) ScreenManager: ScreenManager initialized successfully
```

如果没有这条日志，说明初始化失败。

### 问题 3: 车载数据不更新

**原因**：CarUIHelper 更新任务没有启动

**解决**：确认 application.cc 中已调用：
```cpp
CarUIHelper::StartUpdateTask(500);
```

---

## 🔍 关键代码位置

| 功能 | 文件 | 行号 |
|------|------|------|
| SW3 初始化 | gec_esp32s3_board.cc | 453-459 |
| ScreenManager 初始化 | application.cc | 599-605 |
| CarUIHelper 初始化 | application.cc | 614-633 |
| 屏幕切换逻辑 | car_ui_helper.cc | ToggleScreen() |

---

## 📝 后续扩展

### 扩展 1: SW3 长按功能

```cpp
Button* sw3_button = new Button(GPIO_NUM_11, true, 2000, 0, false);

// 单击：切换屏幕
sw3_button->OnClick([](){
  ToggleCarMonitorScreen();
});

// 长按：打开设置菜单
sw3_button->OnLongPress([](){
  ESP_LOGI(TAG, "SW3 long pressed");
  // 打开设置界面
});
```

### 扩展 2: SW3 双击功能

```cpp
sw3_button->OnDoubleClick([](){
  ESP_LOGI(TAG, "SW3 double clicked");
  // 快速循环所有屏幕
});
```

### 扩展 3: 添加其他按键

例如添加一个用户自定义按键（GPIO9）：
```cpp
Button* custom_button = new Button(GPIO_NUM_9, true, 0, 0, false);
custom_button->OnClick([](){
  // 自定义功能
});
```

---

## ✅ 完成检查清单

- [x] BOOT 按键保持原样（不修改）
- [x] SW3 按键初始化（GPIO11）
- [x] SW3 单击回调连接
- [x] ScreenManager 初始化
- [x] CarUIHelper 初始化
- [x] 构建配置更新
- [x] 文档完成
- [ ] 编译验证（待执行）
- [ ] 烧录测试（待执行）
- [ ] 按键功能测试（待执行）

---

## 🎯 使用总结

**最简单、最安全的方案**：
- ✅ BOOT 按键：完全不改，原功能保留
- ✅ SW3 按键：单击切换屏幕，功能单一
- ✅ 两个系统独立运行，不互相影响

**用户操作**：
1. 正常使用聊天功能（按 BOOT 控制）
2. 需要看车载数据？按一下 SW3
3. 想回聊天？再按一下 SW3

简洁高效！

---

**方案状态**: 代码集成完成，等待编译验证 ✅
