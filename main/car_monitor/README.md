# car_monitor 组件

车载监测系统的核心组件，提供环境监测、人员检测、告警管理等功能。

## 文件结构

```
car_monitor/
├── CMakeLists.txt           # 组件构建配置
├── car_snapshot.h/cc        # 状态快照数据结构
├── virtual_sensors.h/cc     # 虚拟传感器（演示模式）
├── car_monitor.h/cc         # 核心协调器类
└── README.md               # 本文件
```

## 核心类

### 1. CarSnapshot - 状态快照

定义了完整的车载系统状态数据结构，包括：

```cpp
struct CarSnapshot {
    bool demo_mode;                    // 演示模式
    CarVehicleState vehicle_state;     // 车辆状态（行驶/停车）
    EnvironmentData env_data;          // 环境数据（温度/湿度/光照）
    PersonDetectionData person_data;   // 人员检测结果
    bool is_moving;                    // 运动状态
    AlertType active_alert;            // 活跃告警
    // ... 其他字段
};
```

**特点**：
- 一致性快照：所有数据在同一时刻获取
- 线程安全：由互斥锁保护
- 有效期管理：每个数据项都有年龄追踪

### 2. VirtualSensors - 虚拟传感器

在演示模式下生成模拟传感器数据：

```cpp
float temp = VirtualSensors::ReadTemperature();   // 20-35°C，波动±2°C
float humidity = VirtualSensors::ReadHumidity();  // 40-60%，波动±5%
float light = VirtualSensors::ReadLight();        // 30-70%，波动±10%
```

### 3. CarMonitor - 核心协调器

单例模式，管理所有监测逻辑。

**初始化**：
```cpp
// 演示模式
CarMonitor::Init(true);

// 真实传感器模式（后续扩展）
CarMonitor::Init(false);

CarMonitor* monitor = CarMonitor::GetInstance();
```

**启动任务**：
```cpp
monitor->StartMonitorTask(4096, 5);  // 栈大小4KB，优先级5
```

**查询状态**：
```cpp
CarSnapshot snapshot;
if (monitor->GetSnapshot(&snapshot)) {
    ESP_LOGI("temp: %.1f°C", snapshot.env_data.temperature);
}
```

**提交命令**：
```cpp
// 切换车辆启停状态
CarCommand cmd = {CMD_TOGGLE_VEHICLE_STATE, 0};
monitor->SubmitCommand(cmd);

// 确认告警
cmd = {CMD_CONFIRM_ALERT, 0};
monitor->SubmitCommand(cmd);
```

**检测人员**（由外部人员检测模块调用）：
```cpp
// person_count 检测到的人数
// confidence 置信度 (0.0-1.0)
monitor->UpdatePersonDetection(1, 0.95f);
```

## 告警规则

### 1. 人员遗留告警

**触发条件**：
1. 车辆状态为 PARKED（已停车）
2. 停车后超过15秒宽限期
3. 最近5次检测中，至少3次检测到有人

**解除条件**：
- 用户确认告警
- 车辆切换到 RUNNING 状态
- 连续10秒检测到无人

**代码**：
```cpp
// 在 CarMonitor::CheckPersonLeftAlert() 中实现
// - PARKING_GRACE_PERIOD_MS = 15秒
// - PERSON_DETECTION_THRESHOLD = 3/5
// - PERSON_EMPTY_CONFIRM_MS = 10秒
```

### 2. 温度异常（待实现）

**规则**：
- 触发：≥45°C 持续5秒
- 清除：≤42°C 持续10秒（滞回设计）

### 3. 湿度异常（待实现）

**规则**：
- 触发：≥80% 持续10秒
- 清除：≤75% 持续10秒（滞回设计）

## 数据有效期

```cpp
TEMP_VALIDITY_MS = 6000;                    // 温度有效期 6 秒
PERSON_DETECTION_VALIDITY_MS = 2000;        // 人员检测有效期 2 秒
MOTION_VALIDITY_MS = 1000;                  // 运动状态有效期 1 秒
```

数据超过有效期后，valid标志变为false。

## 任务架构

```
Monitor Task (FreeRTOS)
│
├─ UpdateEnvironmentData()    // 读取传感器 + 虚拟数据
├─ ProcessCommand()            // 处理外部命令队列
└─ UpdateAlertStateMachine()   // 评估告警条件
     │
     ├─ CheckPersonLeftAlert()
     ├─ CheckTemperatureAlert()
     └─ CheckHumidityAlert()

外部模块通过：
├─ SubmitCommand()             // 发送命令（异步）
├─ GetSnapshot()               // 查询状态（同步）
├─ UpdatePersonDetection()     // 更新人员检测结果
└─ GetActiveAlert()            // 查询活跃告警
```

## 集成示例

### 在应用程序中使用

```cpp
#include "car_monitor/car_monitor.h"

void app_main() {
    // 初始化
    CarMonitor::Init(true);  // 演示模式
    CarMonitor* monitor = CarMonitor::GetInstance();
    
    // 启动监测任务
    monitor->StartMonitorTask(4096, 5);
    
    // 模拟人员检测（由实际检测模块调用）
    // ... 人员检测代码 ...
    // monitor->UpdatePersonDetection(person_count, confidence);
    
    // 查询状态
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        CarSnapshot snapshot;
        if (monitor->GetSnapshot(&snapshot)) {
            ESP_LOGI("APP", "Temp: %.1f°C, Person: %d",
                     snapshot.env_data.temperature,
                     snapshot.person_data.person_count);
            
            if (snapshot.active_alert != ALERT_NONE) {
                ESP_LOGW("APP", "Alert: %d", (int)snapshot.active_alert);
            }
        }
    }
}
```

## 后续扩展点

### 1. 真实传感器接入
- [ ] SHT31 温湿度传感器驱动
- [ ] ADS1115 + 光敏电阻
- [ ] 实现 `UpdateEnvironmentData()` 真实路径

### 2. IMU集成
- [ ] QMI8658A 驱动
- [ ] 运动检测逻辑
- [ ] `UpdateMotionData()` 方法

### 3. 告警完善
- [ ] 温度/湿度告警逻辑实现
- [ ] 告警优先级机制
- [ ] 告警历史记录

### 4. 存储功能
- [ ] TF卡告警记录
- [ ] 抓拍图像保存
- [ ] 日志导出

## 技术细节

### 线程安全

所有共享数据使用互斥锁保护：

```cpp
SemaphoreHandle_t snapshot_mutex_;   // 快照数据保护
SemaphoreHandle_t instance_mutex_;   // 单例保护
QueueHandle_t command_queue_;        // 命令队列（FreeRTOS自带同步）
```

### 内存占用

- CarSnapshot: ~500 字节
- VirtualSensorGenerator: ~20 字节
- 命令队列（10条）: ~200 字节
- 信号量 × 2: ~100 字节
- **总计**: ~800 字节 （不含任务栈）

### 时间复杂度

- `GetSnapshot()`: O(1)，单次内存复制
- `SubmitCommand()`: O(1)，队列操作
- `UpdatePersonDetection()`: O(1)
- 监测任务周期: 100ms，实时性满足要求

## 调试

### 查看监测状态

```cpp
CarSnapshot snap;
CarMonitor::GetInstance()->GetSnapshot(&snap);

ESP_LOGI("DEBUG", "Demo: %d", snap.demo_mode);
ESP_LOGI("DEBUG", "Vehicle: %d (0=RUN, 1=PARK)", snap.vehicle_state);
ESP_LOGI("DEBUG", "Temp: %.1f (valid=%d, age=%ldms)",
         snap.env_data.temperature,
         snap.env_data.temperature_valid,
         snap.env_data.temperature_age_ms);
ESP_LOGI("DEBUG", "Person: %d (count=%d, conf=%.2f)",
         snap.person_data.state,
         snap.person_data.person_count,
         snap.person_data.confidence);
ESP_LOGI("DEBUG", "Alert: %d (confirmed=%d)", 
         snap.active_alert,
         snap.alert_confirmed);
```

## 已知限制

1. **演示模式数据**：虚拟数据是随机生成的，非真实传感器数据
2. **告警单一**：当前仅实现人员遗留告警，温度/湿度告警待完成
3. **无持久化**：告警历史仅存储在RAM，无TF卡备份
4. **IMU未集成**：运动检测还未实现

## 后续开发计划

### 第一阶段（本周）
- [x] 核心框架搭建
- [x] 虚拟传感器
- [x] 基本告警逻辑
- [ ] LVGL界面集成

### 第二阶段（下周）
- [ ] 真实传感器驱动接入
- [ ] 其他告警规则完成
- [ ] IMU驱动适配

### 第三阶段（后续）
- [ ] TF卡存储
- [ ] HTTP服务器查询
- [ ] OTA升级
