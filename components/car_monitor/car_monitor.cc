#include "car_monitor.h"
#include "virtual_sensors.h"
#include <esp_log.h>
#include <freertos/semphr.h>
#include <cstring>
#include <ctime>

static const char *TAG = "car_monitor";

// ============================================================================
// 静态成员初始化
// ============================================================================

CarMonitor* CarMonitor::instance_ = nullptr;
SemaphoreHandle_t CarMonitor::instance_mutex_ = nullptr;

// ============================================================================
// 告警规则常量
// ============================================================================

// 停车遗留相关
static constexpr uint32_t PARKING_GRACE_PERIOD_MS = 15000;  // 停车后15秒宽限期
static constexpr uint32_t PERSON_DETECTION_WINDOW_MS = 5000; // 5秒内最多5次检测
static constexpr int PERSON_DETECTION_THRESHOLD = 3;         // 5次中至少3次有人
static constexpr uint32_t PERSON_EMPTY_CONFIRM_MS = 10000;   // 连续10秒无人才确认

// 温度异常
static constexpr float TEMP_HIGH_THRESHOLD = 45.0f;  // °C
static constexpr uint32_t TEMP_HIGH_DURATION_MS = 5000; // 持续5秒
static constexpr float TEMP_HIGH_CLEAR = 42.0f;      // 清除阈值（滞回）
static constexpr uint32_t TEMP_HIGH_CLEAR_DURATION_MS = 10000; // 持续10秒

// 湿度异常
static constexpr float HUMIDITY_HIGH_THRESHOLD = 80.0f; // %
static constexpr uint32_t HUMIDITY_HIGH_DURATION_MS = 10000; // 持续10秒
static constexpr float HUMIDITY_HIGH_CLEAR = 75.0f;    // 清除阈值（滞回）
static constexpr uint32_t HUMIDITY_HIGH_CLEAR_DURATION_MS = 10000;

// 数据有效期
static constexpr uint32_t TEMP_VALIDITY_MS = 6000;    // 6秒
static constexpr uint32_t PERSON_DETECTION_VALIDITY_MS = 2000; // 2秒
static constexpr uint32_t MOTION_VALIDITY_MS = 1000;   // 1秒

// 告警确认时长
static constexpr uint32_t ALERT_CONFIRM_DURATION_MS = 30000; // 30秒

// ============================================================================
// 构造和析构
// ============================================================================

CarMonitor::CarMonitor()
    : current_snapshot_(),
      snapshot_mutex_(nullptr),
      command_queue_(nullptr),
      monitor_task_handle_(nullptr),
      demo_mode_(false),
      initialized_(false),
      parking_start_time_(0),
      last_person_detected_time_(0),
      person_detection_count_(0),
      empty_detection_count_(0),
      alert_start_time_(0),
      alert_confirm_time_(0) {
    // 初始化快照
    std::memset(&current_snapshot_, 0, sizeof(CarSnapshot));
    current_snapshot_.demo_mode = false;
    current_snapshot_.vehicle_state = CAR_STATE_RUNNING;
    current_snapshot_.person_data.state = PERSON_STATE_UNKNOWN;
    current_snapshot_.active_alert = ALERT_NONE;
}

CarMonitor::~CarMonitor() {
    if (snapshot_mutex_) {
        vSemaphoreDelete(snapshot_mutex_);
    }
    if (command_queue_) {
        vQueueDelete(command_queue_);
    }
}

// ============================================================================
// 单例接口
// ============================================================================

bool CarMonitor::Init(bool demo_mode) {
    if (instance_mutex_ == nullptr) {
        instance_mutex_ = xSemaphoreCreateMutex();
        if (instance_mutex_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create instance mutex");
            return false;
        }
    }

    xSemaphoreTake(instance_mutex_, portMAX_DELAY);

    if (instance_ == nullptr) {
        instance_ = new CarMonitor();
        if (instance_ == nullptr) {
            xSemaphoreGive(instance_mutex_);
            ESP_LOGE(TAG, "Failed to create CarMonitor instance");
            return false;
        }

        instance_->demo_mode_ = demo_mode;
        instance_->snapshot_mutex_ = xSemaphoreCreateMutex();

        if (instance_->snapshot_mutex_ == nullptr) {
            delete instance_;
            instance_ = nullptr;
            xSemaphoreGive(instance_mutex_);
            ESP_LOGE(TAG, "Failed to create snapshot mutex");
            return false;
        }

        // 创建命令队列（容量10）
        instance_->command_queue_ = xQueueCreate(10, sizeof(CarCommand));
        if (instance_->command_queue_ == nullptr) {
            delete instance_;
            instance_ = nullptr;
            xSemaphoreGive(instance_mutex_);
            ESP_LOGE(TAG, "Failed to create command queue");
            return false;
        }

        // 初始化虚拟传感器（演示模式）
        if (demo_mode) {
            VirtualSensors::Init();
            instance_->current_snapshot_.demo_mode = true;
            ESP_LOGI(TAG, "CarMonitor initialized in DEMO mode");
        } else {
            ESP_LOGI(TAG, "CarMonitor initialized in REAL SENSOR mode");
        }

        instance_->initialized_ = true;
    }

    xSemaphoreGive(instance_mutex_);
    return true;
}

CarMonitor* CarMonitor::GetInstance() {
    if (instance_mutex_ == nullptr) {
        ESP_LOGE(TAG, "CarMonitor not initialized, call Init() first");
        return nullptr;
    }

    xSemaphoreTake(instance_mutex_, portMAX_DELAY);
    CarMonitor* result = instance_;
    xSemaphoreGive(instance_mutex_);

    return result;
}

// ============================================================================
// 快照查询接口
// ============================================================================

bool CarMonitor::GetSnapshot(CarSnapshot* out_snapshot) {
    if (!initialized_ || out_snapshot == nullptr) {
        return false;
    }

    xSemaphoreTake(snapshot_mutex_, portMAX_DELAY);
    std::memcpy(out_snapshot, &current_snapshot_, sizeof(CarSnapshot));
    xSemaphoreGive(snapshot_mutex_);

    return true;
}

// ============================================================================
// 人员检测更新
// ============================================================================

void CarMonitor::UpdatePersonDetection(int person_count, float confidence) {
    if (!initialized_) {
        return;
    }

    xSemaphoreTake(snapshot_mutex_, portMAX_DELAY);

    current_snapshot_.person_data.person_count = person_count;
    current_snapshot_.person_data.confidence = confidence;
    current_snapshot_.person_data.valid = true;
    current_snapshot_.person_data.last_detection_time = (uint32_t)esp_timer_get_time() / 1000;
    current_snapshot_.person_data.detection_age_ms = 0;

    // 根据检测结果更新人员状态
    if (person_count > 0) {
        current_snapshot_.person_data.state = PERSON_STATE_PRESENT;
        last_person_detected_time_ = current_snapshot_.person_data.last_detection_time;
    } else {
        // 检测到无人，暂不立即改状态，等待确认
        // 状态更新由状态机处理
    }

    xSemaphoreGive(snapshot_mutex_);
}

// ============================================================================
// 运动状态更新（姿态传感器 QMI8658A，辅助状态）
// ============================================================================

void CarMonitor::UpdateMotionState(bool is_moving, bool imu_fault) {
    if (!initialized_) {
        return;
    }

    xSemaphoreTake(snapshot_mutex_, portMAX_DELAY);
    current_snapshot_.is_moving = is_moving;
    current_snapshot_.motion_age_ms = 0;   // 刚刚更新
    current_snapshot_.imu_fault = imu_fault;
    xSemaphoreGive(snapshot_mutex_);
}

// ============================================================================
// 告警接口
// ============================================================================

AlertType CarMonitor::GetActiveAlert() {
    if (!initialized_) {
        return ALERT_NONE;
    }

    xSemaphoreTake(snapshot_mutex_, portMAX_DELAY);
    AlertType alert = current_snapshot_.active_alert;
    xSemaphoreGive(snapshot_mutex_);

    return alert;
}

void CarMonitor::ClearActiveAlert() {
    if (!initialized_) {
        return;
    }

    xSemaphoreTake(snapshot_mutex_, portMAX_DELAY);
    current_snapshot_.active_alert = ALERT_NONE;
    current_snapshot_.alert_confirmed = false;
    xSemaphoreGive(snapshot_mutex_);

    ESP_LOGI(TAG, "Alert cleared");
}

// ============================================================================
// 任务管理
// ============================================================================

bool CarMonitor::StartMonitorTask(uint32_t stack_size, uint32_t priority) {
    if (!initialized_ || monitor_task_handle_ != nullptr) {
        ESP_LOGE(TAG, "Monitor task already running or not initialized");
        return false;
    }

    BaseType_t ret = xTaskCreate(
        MonitorTaskFunc,
        "car_monitor",
        stack_size,
        this,
        priority,
        &monitor_task_handle_
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitor task");
        return false;
    }

    ESP_LOGI(TAG, "Monitor task started");
    return true;
}

TaskHandle_t CarMonitor::GetMonitorTaskHandle() {
    return monitor_task_handle_;
}

// ============================================================================
// 命令提交
// ============================================================================

bool CarMonitor::SubmitCommand(const CarCommand& cmd) {
    if (!initialized_ || command_queue_ == nullptr) {
        return false;
    }

    BaseType_t ret = xQueueSend(command_queue_, &cmd, 0);
    return ret == pdPASS;
}

// ============================================================================
// 内部状态更新函数（下一个文件中继续）
// ============================================================================

void CarMonitor::UpdateEnvironmentData() {
    if (!initialized_) {
        return;
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    xSemaphoreTake(snapshot_mutex_, portMAX_DELAY);

    // 更新环境数据
    if (demo_mode_) {
        current_snapshot_.env_data.temperature = VirtualSensors::ReadTemperature();
        current_snapshot_.env_data.temperature_valid = true;
        current_snapshot_.env_data.temperature_age_ms = 0;

        current_snapshot_.env_data.humidity = VirtualSensors::ReadHumidity();
        current_snapshot_.env_data.humidity_valid = true;
        current_snapshot_.env_data.humidity_age_ms = 0;

        current_snapshot_.env_data.light_percent = VirtualSensors::ReadLight();
        current_snapshot_.env_data.light_valid = true;
        current_snapshot_.env_data.light_age_ms = 0;
    } else {
        // 真实传感器接入时在这里实现
        // 暂时保持为上次的值
    }

    current_snapshot_.snapshot_id++;
    current_snapshot_.timestamp = now;

    xSemaphoreGive(snapshot_mutex_);
}

// ============================================================================
// 监测任务主函数
// ============================================================================

void CarMonitor::MonitorTaskFunc(void* arg) {
    CarMonitor* self = static_cast<CarMonitor*>(arg);

    ESP_LOGI(TAG, "Monitor task started");

    // 主循环：100ms周期
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));

        // 1. 更新环境数据
        self->UpdateEnvironmentData();

        // 2. 处理命令队列中的所有命令
        CarCommand cmd;
        while (xQueueReceive(self->command_queue_, &cmd, 0) == pdTRUE) {
            self->ProcessCommand(cmd);
        }

        // 3. 更新告警状态机
        self->UpdateAlertStateMachine();
    }
}

// ============================================================================
// 命令处理和状态更新（继续在下一个部分）
// ============================================================================

void CarMonitor::ProcessCommand(const CarCommand& cmd) {
    ESP_LOGD(TAG, "Processing command type=%d", (int)cmd.type);

    xSemaphoreTake(snapshot_mutex_, portMAX_DELAY);

    switch (cmd.type) {
    case CMD_TOGGLE_VEHICLE_STATE: {
        if (current_snapshot_.vehicle_state == CAR_STATE_RUNNING) {
            current_snapshot_.vehicle_state = CAR_STATE_PARKED;
            parking_start_time_ = (uint32_t)(esp_timer_get_time() / 1000);
            person_detection_count_ = 0;
            empty_detection_count_ = 0;
            ESP_LOGI(TAG, "Vehicle state: PARKED");
        } else {
            current_snapshot_.vehicle_state = CAR_STATE_RUNNING;
            current_snapshot_.person_data.state = PERSON_STATE_UNKNOWN;
            current_snapshot_.active_alert = ALERT_NONE;
            ESP_LOGI(TAG, "Vehicle state: RUNNING");
        }
        break;
    }

    case CMD_CONFIRM_ALERT: {
        if (current_snapshot_.active_alert != ALERT_NONE) {
            current_snapshot_.alert_confirmed = true;
            alert_confirm_time_ = (uint32_t)(esp_timer_get_time() / 1000);
            ESP_LOGI(TAG, "Alert confirmed, will mute for 30s");
        }
        break;
    }

    case CMD_TRIGGER_TEST_ALERT: {
        current_snapshot_.active_alert = ALERT_PERSON_LEFT;
        alert_start_time_ = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGI(TAG, "Test alert triggered");
        break;
    }

    default:
        break;
    }

    xSemaphoreGive(snapshot_mutex_);
}

void CarMonitor::UpdateAlertStateMachine() {
    if (!initialized_) {
        return;
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    xSemaphoreTake(snapshot_mutex_, portMAX_DELAY);

    // 处理告警确认超时（30秒后自动清除确认状态）
    if (current_snapshot_.alert_confirmed &&
        (now - alert_confirm_time_) > ALERT_CONFIRM_DURATION_MS) {
        current_snapshot_.alert_confirmed = false;
    }

    // 检查各项告警条件
    if (!current_snapshot_.alert_confirmed) {
        if (CheckPersonLeftAlert()) {
            if (current_snapshot_.active_alert != ALERT_PERSON_LEFT) {
                current_snapshot_.active_alert = ALERT_PERSON_LEFT;
                alert_start_time_ = now;
                ESP_LOGW(TAG, "ALERT: Person left!");
            }
        }
    }

    xSemaphoreGive(snapshot_mutex_);
}

bool CarMonitor::CheckPersonLeftAlert() {
    // 只在停车状态下检查
    if (current_snapshot_.vehicle_state != CAR_STATE_PARKED) {
        return false;
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    // 停车宽限期未过，不告警
    if ((now - parking_start_time_) < PARKING_GRACE_PERIOD_MS) {
        return false;
    }

    // 人员检测数据无效，不告警
    if (!current_snapshot_.person_data.valid ||
        (now - current_snapshot_.person_data.last_detection_time) > PERSON_DETECTION_VALIDITY_MS) {
        return false;
    }

    // 检测到有人，记录
    if (current_snapshot_.person_data.person_count > 0) {
        person_detection_count_++;
        empty_detection_count_ = 0;

        // 5次检测中至少3次有人，则触发告警
        if (person_detection_count_ >= PERSON_DETECTION_THRESHOLD) {
            return true;
        }
    } else {
        // 检测到无人
        empty_detection_count_++;
        person_detection_count_ = 0;
    }

    return false;
}

bool CarMonitor::CheckTemperatureAlert() {
    // 实现温度告警逻辑
    // TODO: 后续实现
    return false;
}

bool CarMonitor::CheckHumidityAlert() {
    // 实现湿度告警逻辑
    // TODO: 后续实现
    return false;
}

void CarMonitor::HandleVehicleStateChange() {
    // 处理车辆状态变化的相关逻辑
    // TODO: 后续根据需要实现
}
