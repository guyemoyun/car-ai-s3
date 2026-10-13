# car_monitor 组件集成指南

## 概述

`car_monitor` 是车载监测系统的核心组件，已创建在 `main/car_monitor/` 目录。

**关键特性**：
- ✅ 虚拟传感器数据生成（演示模式）
- ✅ 状态快照管理（线程安全）
- ✅ 人员遗留告警逻辑
- ✅ FreeRTOS任务驱动
- ✅ 命令队列接口

**代码量**：1276行，分8个文件

---

## 快速集成步骤

### Step 1：构建验证

在 VS Code 中：
1. 打开命令面板：`Ctrl+Shift+P`
2. 运行：`ESP-IDF: Build Your Project`
3. 预期：编译通过，无error

**如果编译失败**，常见原因：
- 缺少 `<cmath>` 或 `<cstdlib>` 头文件 → 检查car_monitor.cc包含
- 链接错误 → 确保CMakeLists.txt正确

---

### Step 2：在应用程序中初始化

打开 `main/application.cc` 或 `main/main.cc`，在启动代码中添加：

```cpp
#include "car_monitor/car_monitor.h"

void setup_car_monitor() {
    // 演示模式：使用虚拟数据
    if (!CarMonitor::Init(true)) {
        ESP_LOGE(TAG, "Failed to initialize CarMonitor");
        return;
    }
    
    CarMonitor* monitor = CarMonitor::GetInstance();
    
    // 启动监测任务（优先级5，栈大小4KB）
    if (!monitor->StartMonitorTask(4096, 5)) {
        ESP_LOGE(TAG, "Failed to start monitor task");
        return;
    }
    
    ESP_LOGI(TAG, "CarMonitor started");
}

// 在app_main()或初始化代码中调用
void app_main() {
    // ... 其他初始化代码 ...
    
    setup_car_monitor();  // 添加这一行
    
    // ... 其他代码 ...
}
```

---

### Step 3：查询状态

在你的业务逻辑中查询监测状态：

```cpp
void monitor_task(void* arg) {
    CarMonitor* monitor = CarMonitor::GetInstance();
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        CarSnapshot snapshot;
        if (monitor->GetSnapshot(&snapshot)) {
            // 打印环境数据
            ESP_LOGI("MONITOR", 
                     "Temp: %.1f°C, Humidity: %.1f%%, Light: %.1f%%",
                     snapshot.env_data.temperature,
                     snapshot.env_data.humidity,
                     snapshot.env_data.light_percent);
            
            // 打印人员检测
            ESP_LOGI("MONITOR", 
                     "Person state: %d, Count: %d, Confidence: %.2f",
                     (int)snapshot.person_data.state,
                     snapshot.person_data.person_count,
                     snapshot.person_data.confidence);
            
            // 检查告警
            if (snapshot.active_alert != ALERT_NONE) {
                ESP_LOGW("MONITOR", "ALERT TYPE: %d", (int)snapshot.active_alert);
            }
        }
    }
}
```

---

### Step 4：处理用户交互

当用户按下按键时，发送命令：

```cpp
// GPIO11 按键（用户按钮）
void handle_user_button_press() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) return;
    
    // 切换车辆启停状态
    CarCommand cmd = {CMD_TOGGLE_VEHICLE_STATE, 0};
    monitor->SubmitCommand(cmd);
}

// 触摸屏确认告警按钮
void handle_confirm_alert_button() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) return;
    
    CarCommand cmd = {CMD_CONFIRM_ALERT, 0};
    monitor->SubmitCommand(cmd);
}

// 用于测试的按键
void handle_test_alert_button() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) return;
    
    CarCommand cmd = {CMD_TRIGGER_TEST_ALERT, 0};
    monitor->SubmitCommand(cmd);
}
```

---

### Step 5：集成人员检测模块

当你的人员检测模块（基于摄像头）获得检测结果时，调用：

```cpp
// 在人员检测完成回调中
void on_person_detection_result(int person_count, float confidence) {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor != nullptr) {
        monitor->UpdatePersonDetection(person_count, confidence);
    }
}
```

---

## 当前实现的功能

### ✅ 已实现

| 功能 | 状态 | 说明 |
|------|------|------|
| 虚拟温湿度数据 | ✅ 完成 | 演示模式生成随机数据 |
| 虚拟光照数据 | ✅ 完成 | 演示模式生成随机数据 |
| 快照查询 | ✅ 完成 | 获取一致的系统状态 |
| 车辆启停切换 | ✅ 完成 | 通过命令队列控制 |
| 人员检测输入 | ✅ 完成 | 接收外部检测结果 |
| 人员遗留告警 | ✅ 完成 | 停车15秒后检测有人触发 |
| 告警确认 | ✅ 完成 | 用户可确认告警，静音30秒 |
| 监测任务 | ✅ 完成 | FreeRTOS任务驱动 |

### ⏳ 待实现

| 功能 | 优先级 | 说明 |
|------|--------|------|
| 真实传感器驱动 | 高 | SHT31、ADS1115接入 |
| 温度异常告警 | 中 | ≥45°C触发 |
| 湿度异常告警 | 中 | ≥80%触发 |
| IMU集成 | 低 | 运动检测辅助 |
| 告警历史 | 中 | TF卡或内存缓存 |
| HTTP查询 | 低 | 远程访问告警记录 |

---

## 数据流示例

```
用户按下GPIO11按键
    ↓
应用程序 → submit_command(CMD_TOGGLE_VEHICLE_STATE)
    ↓
命令队列 (xQueueSend)
    ↓
Monitor任务 (100ms周期)
    ├─ ProcessCommand() → 处理命令，更改vehicle_state
    ├─ UpdateEnvironmentData() → 读虚拟传感器
    ├─ UpdateAlertStateMachine() → 评估告警条件
    └─ 每个循环更新一次 snapshot
    ↓
应用程序 → GetSnapshot()
    ↓
获得最新状态，更新LVGL界面
```

---

## 调试技巧

### 1. 查看监测系统状态

添加一个调试任务：

```cpp
void debug_car_monitor_task(void* arg) {
    CarMonitor* monitor = CarMonitor::GetInstance();
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));  // 5秒输出一次
        
        CarSnapshot snap;
        if (monitor->GetSnapshot(&snap)) {
            ESP_LOGI("CAR_MONITOR_DEBUG", "=== System State ===");
            ESP_LOGI("CAR_MONITOR_DEBUG", "Demo Mode: %d", snap.demo_mode);
            ESP_LOGI("CAR_MONITOR_DEBUG", "Vehicle: %s", 
                     snap.vehicle_state == CAR_STATE_RUNNING ? "RUNNING" : 
                     snap.vehicle_state == CAR_STATE_PARKED ? "PARKED" : "UNKNOWN");
            ESP_LOGI("CAR_MONITOR_DEBUG", "Temp: %.1f°C (valid=%d, age=%ldms)",
                     snap.env_data.temperature,
                     snap.env_data.temperature_valid,
                     snap.env_data.temperature_age_ms);
            ESP_LOGI("CAR_MONITOR_DEBUG", "Humidity: %.1f%% (valid=%d)",
                     snap.env_data.humidity,
                     snap.env_data.humidity_valid);
            ESP_LOGI("CAR_MONITOR_DEBUG", "Light: %.1f%% (valid=%d)",
                     snap.env_data.light_percent,
                     snap.env_data.light_valid);
            ESP_LOGI("CAR_MONITOR_DEBUG", "Person State: %d, Count: %d, Conf: %.2f",
                     (int)snap.person_data.state,
                     snap.person_data.person_count,
                     snap.person_data.confidence);
            ESP_LOGI("CAR_MONITOR_DEBUG", "Active Alert: %d", (int)snap.active_alert);
            ESP_LOGI("CAR_MONITOR_DEBUG", "Alert Confirmed: %d", snap.alert_confirmed);
        }
    }
}

// 在app_main中启动
xTaskCreate(debug_car_monitor_task, "debug_monitor", 2048, NULL, 4, NULL);
```

### 2. 模拟告警触发

```cpp
// 通过串口命令或按键触发测试告警
void trigger_test_alert() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    CarCommand cmd = {CMD_TRIGGER_TEST_ALERT, 0};
    monitor->SubmitCommand(cmd);
    ESP_LOGI("TEST", "Test alert triggered");
}
```

### 3. 模拟人员检测

```cpp
// 模拟检测到1个人
void simulate_person_detected() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    monitor->UpdatePersonDetection(1, 0.95f);
}

// 模拟无人
void simulate_no_person() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    monitor->UpdatePersonDetection(0, 0.0f);
}
```

---

## 常见问题

### Q1：编译时出现"undefined reference to VirtualSensors"

**A**：确保 CMakeLists.txt 中包含了 virtual_sensors.cc

### Q2：GetSnapshot() 总是返回false

**A**：确保先调用了 `CarMonitor::Init()`，且传入的指针有效

### Q3：虚拟数据不更新

**A**：确保启动了 Monitor 任务，且FreeRTOS调度正常

### Q4：告警没有触发

**A**：检查以下条件：
- 车辆状态是否为 PARKED
- 停车时间是否超过15秒
- 是否调用了 `UpdatePersonDetection()`
- 是否已确认前一个告警

---

## 下一步

### 立即可做
1. ✅ 构建验证
2. ✅ 集成到应用程序
3. ✅ 查询虚拟数据
4. ✅ 测试命令处理

### 本周
5. 集成人员检测模块
6. 集成LVGL界面显示
7. 集成语音播报告警

### 下周
8. 移植真实传感器驱动
9. 完成温度/湿度告警
10. 集成IMU运动检测

---

## 文件清单

```
main/car_monitor/
├── CMakeLists.txt              # 构建配置（自动被ESP-IDF发现）
├── car_snapshot.h              # 状态快照数据结构 (174行)
├── car_snapshot.cc             # 状态快照实现 (12行)
├── virtual_sensors.h           # 虚拟传感器接口 (48行)
├── virtual_sensors.cc          # 虚拟传感器实现 (69行)
├── car_monitor.h               # 核心类接口 (204行)
├── car_monitor.cc              # 核心类实现 (466行)
└── README.md                   # 组件文档 (291行)
```

---

## 技术支持

如遇问题，检查以下文件：
- 详细文档：`main/car_monitor/README.md`
- 头文件注释：`main/car_monitor/*.h`
- 实现细节：`main/car_monitor/*.cc`
