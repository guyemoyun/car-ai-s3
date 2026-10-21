/**
 * @file car_monitor_helper.cc
 * @brief 车载监测系统集成辅助函数实现
 */

#include "car_monitor_helper.h"

#include "application.h"   // Application::GetDeviceState()（get_status 里推导"是否在会话"）
#include "mcp_server.h"  // McpServer / PropertyList / ReturnValue（车载 MCP 工具注册用）

#include <esp_timer.h>
#include <cstdio>
#include <string>

static const char* TAG = "CarHelper";

namespace {

/** @brief 取一次一致性快照；CarMonitor 未初始化或取快照失败返回 false */
bool ReadSnapshot(CarSnapshot* out) {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr || out == nullptr) {
        return false;
    }
    return monitor->GetSnapshot(out);
}

/**
 * @brief 把浮点数值写成一位小数字符串
 * @note 本工程 newlib 为 nano 格式，`%f` / `std::to_string(float)` 不可靠，
 *       故用整数运算拼出文本（如 25.3、-3.0）。
 */
void FormatFixed1(char* out, size_t out_size, float value) {
    int v10 = (int)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    if (v10 < 0) {
        snprintf(out, out_size, "-%d.%d", (-v10) / 10, (-v10) % 10);
    } else {
        snprintf(out, out_size, "%d.%d", v10 / 10, v10 % 10);
    }
}

const char* VehicleStateName(CarVehicleState state) {
    switch (state) {
        case CAR_STATE_RUNNING: return "RUNNING";
        case CAR_STATE_PARKED:  return "PARKED";
        default:                return "UNKNOWN";
    }
}

const char* PersonStateName(CarPersonState state) {
    switch (state) {
        case PERSON_STATE_EMPTY:   return "EMPTY";
        case PERSON_STATE_PRESENT: return "PRESENT";
        default:                   return "UNKNOWN";
    }
}

const char* AlertTypeName(AlertType type) {
    switch (type) {
        case ALERT_PERSON_LEFT:     return "person_left";
        case ALERT_TEMP_HIGH:       return "temp_high";
        case ALERT_HUMIDITY_HIGH:   return "humidity_high";
        case ALERT_LIGHT_ABNORMAL:  return "light_abnormal";
        default:                    return "none";
    }
}

/** @brief 布尔转 JSON 字面量 */
const char* JsonBool(bool value) { return value ? "true" : "false"; }

/** @brief 当前毫秒时钟（与 car_monitor 记录时间戳所用时基一致） */
uint32_t NowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

}  // namespace

/**
 * @brief 查询并打印车载监测状态（用于调试）
 * @details 读取当前状态快照并通过日志输出
 */
void CarMonitorHelper::PrintStatus() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) {
        ESP_LOGW(TAG, "CarMonitor not initialized");
        return;
    }

    CarSnapshot snapshot;
    if (!monitor->GetSnapshot(&snapshot)) {
        ESP_LOGW(TAG, "Failed to get snapshot");
        return;
    }

    ESP_LOGI(TAG, "=== 车载监测状态 ===");
    ESP_LOGI(TAG, "演示模式: %s", snapshot.demo_mode ? "是" : "否");
    ESP_LOGI(TAG, "车辆状态: %s",
             snapshot.vehicle_state == CAR_STATE_RUNNING ? "行驶中" :
             snapshot.vehicle_state == CAR_STATE_PARKED ? "已停车" : "未知");

    // 环境数据
    if (snapshot.env_data.temperature_valid) {
        ESP_LOGI(TAG, "温度: %.1f°C", snapshot.env_data.temperature);
    } else {
        ESP_LOGI(TAG, "温度: 数据无效");
    }

    if (snapshot.env_data.humidity_valid) {
        ESP_LOGI(TAG, "湿度: %.1f%%", snapshot.env_data.humidity);
    } else {
        ESP_LOGI(TAG, "湿度: 数据无效");
    }

    if (snapshot.env_data.light_valid) {
        ESP_LOGI(TAG, "光照: %.1f%%", snapshot.env_data.light_percent);
    } else {
        ESP_LOGI(TAG, "光照: 数据无效");
    }

    // 人员检测
    ESP_LOGI(TAG, "人员状态: %s",
             snapshot.person_data.state == PERSON_STATE_UNKNOWN ? "未知" :
             snapshot.person_data.state == PERSON_STATE_EMPTY ? "无人" : "有人");
    ESP_LOGI(TAG, "检测人数: %d, 置信度: %.2f",
             snapshot.person_data.person_count,
             snapshot.person_data.confidence);

    // 告警状态
    if (snapshot.active_alert != ALERT_NONE) {
        const char* alert_name = "未知";
        switch (snapshot.active_alert) {
            case ALERT_PERSON_LEFT: alert_name = "人员遗留"; break;
            case ALERT_TEMP_HIGH: alert_name = "温度过高"; break;
            case ALERT_HUMIDITY_HIGH: alert_name = "湿度过高"; break;
            case ALERT_LIGHT_ABNORMAL: alert_name = "光照异常"; break;
            default: break;
        }
        ESP_LOGW(TAG, "活跃告警: %s (已确认: %s)",
                 alert_name,
                 snapshot.alert_confirmed ? "是" : "否");
    } else {
        ESP_LOGI(TAG, "告警状态: 无活跃告警");
    }
}

/**
 * @brief 提交"切换车辆启停状态"命令（行驶中 ⇄ 停车）
 * @details 只往命令队列投递 CMD_TOGGLE_VEHICLE_STATE，由 car_monitor 任务异步生效。
 * @note 当前唯一可用入口是 MCP 工具 `self.car.toggle_vehicle_state`（另有屏幕按钮接线保留）。
 * @warning 本板 GPIO11 被 SW3 按键占用（切换屏幕），不是车辆状态按键；
 *          旧注释"处理用户按键（GPIO11）"已作废，别再按它去找按钮。
 */
void CarMonitorHelper::HandleUserButtonPress() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) {
        ESP_LOGW(TAG, "CarMonitor未初始化");
        return;
    }

    // 创建切换车辆状态的命令
    CarCommand cmd;
    cmd.type = CMD_TOGGLE_VEHICLE_STATE;
    cmd.param = 0;

    if (monitor->SubmitCommand(cmd)) {
        ESP_LOGI(TAG, "车辆状态切换命令已提交");
    } else {
        ESP_LOGE(TAG, "车辆状态切换命令提交失败（队列满）");
    }
}

/**
 * @brief 处理触摸屏确认告警按钮
 * @details 当用户在屏幕上点击"确认告警"按钮时调用
 * @note 告警将被静音30秒，新的告警仍会正常提示
 */
void CarMonitorHelper::HandleConfirmAlertButton() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) {
        ESP_LOGW(TAG, "CarMonitor未初始化");
        return;
    }

    // 创建确认告警的命令
    CarCommand cmd;
    cmd.type = CMD_CONFIRM_ALERT;
    cmd.param = 0;

    if (monitor->SubmitCommand(cmd)) {
        ESP_LOGI(TAG, "告警确认命令已提交，将静音30秒");
    } else {
        ESP_LOGE(TAG, "告警确认命令提交失败");
    }
}

/**
 * @brief 触发测试告警（用于开发调试）
 * @details 手动触发一个人员遗留告警，用于测试告警流程
 */
void CarMonitorHelper::HandleTestAlertButton() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) {
        ESP_LOGW(TAG, "CarMonitor未初始化");
        return;
    }

    // 创建测试告警命令
    CarCommand cmd;
    cmd.type = CMD_TRIGGER_TEST_ALERT;
    cmd.param = 0;

    if (monitor->SubmitCommand(cmd)) {
        ESP_LOGI(TAG, "测试告警已触发");
    } else {
        ESP_LOGE(TAG, "测试告警触发失败");
    }
}

/**
 * @brief 更新人员检测结果（由人员检测模块调用）
 * @param person_count 检测到的人数（0=无人，1或更多=有人）
 * @param confidence 检测置信度（0.0-1.0）
 * @details 人员检测模块每次完成检测后调用此函数更新结果
 * @note 此函数会被人员检测模块在检测完成回调中调用
 */
void CarMonitorHelper::OnPersonDetectionResult(int person_count, float confidence) {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) {
        ESP_LOGW(TAG, "CarMonitor未初始化");
        return;
    }

    // 更新人员检测结果
    monitor->UpdatePersonDetection(person_count, confidence);

    ESP_LOGD(TAG, "人员检测结果已更新: 人数=%d, 置信度=%.2f",
             person_count, confidence);
}

/**
 * @brief 更新车辆运动状态（由姿态传感器模块调用）
 * @param is_moving 当前是否处于运动/振动状态
 * @param imu_fault true=姿态传感器不可用
 */
void CarMonitorHelper::UpdateMotionState(bool is_moving, bool imu_fault) {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) {
        return;
    }
    monitor->UpdateMotionState(is_moving, imu_fault);
    ESP_LOGD(TAG, "运动状态已更新: moving=%d imu_fault=%d", (int)is_moving, (int)imu_fault);
}

/**
 * @brief 更新网络链路状态（由云上报模块每秒调用）
 */
void CarMonitorHelper::UpdateNetworkState(bool wifi_connected, bool cloud_connected) {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) {
        return;
    }
    monitor->UpdateNetworkState(wifi_connected, cloud_connected);
}

/**
 * @brief 检查是否有活跃告警
 * @return 当前活跃的告警类型（ALERT_NONE表示无告警）
 */
AlertType CarMonitorHelper::CheckActiveAlert() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) {
        return ALERT_NONE;
    }

    return monitor->GetActiveAlert();
}

/**
 * @brief 获取当前车辆状态
 * @return 车辆状态（RUNNING=行驶中，PARKED=已停车）
 */
CarVehicleState CarMonitorHelper::GetVehicleState() {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) {
        return CAR_STATE_UNKNOWN;
    }

    CarSnapshot snapshot;
    if (!monitor->GetSnapshot(&snapshot)) {
        return CAR_STATE_UNKNOWN;
    }

    return snapshot.vehicle_state;
}

/**
 * @brief 获取当前环境数据
 * @param temp 输出温度值（°C）
 * @param humidity 输出湿度值（%）
 * @param light 输出光照值（%）
 * @return true=数据有效，false=数据无效或未初始化
 */
bool CarMonitorHelper::GetEnvironmentData(float* temp, float* humidity, float* light) {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr || temp == nullptr || humidity == nullptr || light == nullptr) {
        return false;
    }

    CarSnapshot snapshot;
    if (!monitor->GetSnapshot(&snapshot)) {
        return false;
    }

    // 检查数据是否有效
    if (!snapshot.env_data.temperature_valid ||
        !snapshot.env_data.humidity_valid ||
        !snapshot.env_data.light_valid) {
        return false;
    }

    // 输出数据
    *temp = snapshot.env_data.temperature;
    *humidity = snapshot.env_data.humidity;
    *light = snapshot.env_data.light_percent;

    return true;
}

/**
 * @brief 获取人员检测状态
 * @param person_count 输出检测到的人数
 * @param confidence 输出检测置信度
 * @return true=数据有效，false=数据无效或未初始化
 */
bool CarMonitorHelper::GetPersonDetectionData(int* person_count, float* confidence) {
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr || person_count == nullptr || confidence == nullptr) {
        return false;
    }

    CarSnapshot snapshot;
    if (!monitor->GetSnapshot(&snapshot)) {
        return false;
    }

    // 检查人员检测数据是否有效
    if (!snapshot.person_data.valid) {
        return false;
    }

    // 输出数据
    *person_count = snapshot.person_data.person_count;
    *confidence = snapshot.person_data.confidence;

    return true;
}

/**
 * @brief 注册车载 MCP 工具（self.car.*）
 * @details 五个工具各司其职：环境查询、状态查询、告警查询、确认告警、切换车辆状态。
 *          所有回调只读一致快照（GetSnapshot）或提交命令（SubmitCommand），
 *          两者在 CarMonitor 内部均有锁保护，可安全地在 MCP 协议线程调用。
 * @note 数值一律用整数运算格式化成文本，避免 nano newlib 下 %f 失效。
 */
void CarMonitorHelper::RegisterMcpTools() {
    McpServer& mcp = McpServer::GetInstance();

    // ---- 1) 环境查询：温度 / 湿度 / 光照（含有效性与数据年龄）----
    mcp.AddTool("self.car.get_environment",
        "Get the car cabin environment from the on-board monitor: temperature (℃), "
        "humidity (%), light level (%), each with a validity flag and reading age in ms. "
        "Use this when the user asks about the temperature, humidity or light inside the car, "
        "or whether those readings are still fresh.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            CarSnapshot s;
            if (!ReadSnapshot(&s)) {
                return std::string("{\"success\": false, \"message\": \"car monitor not initialized\"}");
            }
            char temp[16], hum[16], light[16];
            FormatFixed1(temp, sizeof(temp), s.env_data.temperature);
            FormatFixed1(hum, sizeof(hum), s.env_data.humidity);
            FormatFixed1(light, sizeof(light), s.env_data.light_percent);

            char buf[384];
            snprintf(buf, sizeof(buf),
                     "{\"success\": true, \"demo_mode\": %s, "
                     "\"temperature\": %s, \"temperature_valid\": %s, \"temperature_age_ms\": %u, "
                     "\"humidity\": %s, \"humidity_valid\": %s, \"humidity_age_ms\": %u, "
                     "\"light_percent\": %s, \"light_valid\": %s, \"light_age_ms\": %u}",
                     JsonBool(s.demo_mode),
                     temp, JsonBool(s.env_data.temperature_valid), (unsigned)s.env_data.temperature_age_ms,
                     hum, JsonBool(s.env_data.humidity_valid), (unsigned)s.env_data.humidity_age_ms,
                     light, JsonBool(s.env_data.light_valid), (unsigned)s.env_data.light_age_ms);
            return std::string(buf);
        });

    // ---- 2) 状态查询：车辆 / 人员 / 告警 / 故障 / 网络 ----
    mcp.AddTool("self.car.get_status",
        "Get the overall car monitoring status: vehicle state (RUNNING/PARKED/UNKNOWN), "
        "person state (UNKNOWN/EMPTY/PRESENT) with detected person count, confidence and result age, "
        "active alert type, sensor/camera/audio/I2C fault flags, and network connectivity. "
        "Use this when the user asks about the car's overall state, whether anyone is left inside, "
        "or whether something is faulty.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            CarSnapshot s;
            if (!ReadSnapshot(&s)) {
                return std::string("{\"success\": false, \"message\": \"car monitor not initialized\"}");
            }
            char conf[16];
            FormatFixed1(conf, sizeof(conf), s.person_data.confidence);

            // in_ai_chat_mode 快照字段无人写入，这里直接按设备状态推导（Idle/Unknown 视为不在会话）
            const DeviceState state = Application::GetInstance().GetDeviceState();
            const bool in_ai_chat = (state != kDeviceStateIdle && state != kDeviceStateUnknown);

            char buf[640];
            snprintf(buf, sizeof(buf),
                     "{\"success\": true, \"demo_mode\": %s, "
                     "\"vehicle_state\": \"%s\", "
                     "\"person_state\": \"%s\", \"person_count\": %d, "
                     "\"person_confidence\": %s, \"person_valid\": %s, \"person_age_ms\": %u, "
                     "\"moving\": %s, \"motion_age_ms\": %u, "
                     "\"active_alert\": \"%s\", \"alert_confirmed\": %s, "
                     "\"faults\": {\"temperature_sensor\": %s, \"humidity_sensor\": %s, "
                     "\"camera\": %s, \"audio\": %s, \"i2c\": %s, \"imu\": %s}, "
                     "\"wifi_connected\": %s, \"cloud_connected\": %s, \"in_ai_chat_mode\": %s}",
                     JsonBool(s.demo_mode),
                     VehicleStateName(s.vehicle_state),
                     PersonStateName(s.person_data.state), s.person_data.person_count,
                     conf, JsonBool(s.person_data.valid), (unsigned)s.person_data.detection_age_ms,
                     JsonBool(s.is_moving), (unsigned)s.motion_age_ms,
                     AlertTypeName(s.active_alert), JsonBool(s.alert_confirmed),
                     JsonBool(s.temp_sensor_fault), JsonBool(s.humidity_sensor_fault),
                     JsonBool(s.camera_fault), JsonBool(s.audio_fault), JsonBool(s.i2c_fault),
                     JsonBool(s.imu_fault),
                     JsonBool(s.wifi_connected), JsonBool(s.cloud_connected),
                     JsonBool(in_ai_chat));
            return std::string(buf);
        });

    // ---- 3) 告警查询：当前活跃告警的类型、触发时间与确认状态 ----
    mcp.AddTool("self.car.get_alert",
        "Get the currently active car alert: type (person_left / temp_high / humidity_high / "
        "light_abnormal / none), how long ago it was raised, and whether the user already confirmed it. "
        "Use this when the user asks what the alarm is about, or whether there is any alarm right now.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            CarSnapshot s;
            if (!ReadSnapshot(&s)) {
                return std::string("{\"success\": false, \"message\": \"car monitor not initialized\"}");
            }
            const bool has_alert = (s.active_alert != ALERT_NONE);
            // 时间戳与 car_monitor 同源（esp_timer 毫秒），可直接相减得到年龄
            const uint32_t age_ms = has_alert ? (NowMs() - s.active_alert_timestamp) : 0;

            char buf[192];
            snprintf(buf, sizeof(buf),
                     "{\"success\": true, \"has_alert\": %s, \"alert_type\": \"%s\", "
                     "\"alert_confirmed\": %s, \"alert_age_ms\": %u}",
                     JsonBool(has_alert), AlertTypeName(s.active_alert),
                     JsonBool(s.alert_confirmed), (unsigned)age_ms);
            return std::string(buf);
        });

    // ---- 4) 确认告警：静音当前告警 30 秒（界面告警与云端状态保留）----
    mcp.AddTool("self.car.confirm_alert",
        "Confirm (acknowledge) the current car alert. This mutes the local alarm for 30 seconds "
        "while the alert stays visible on screen and the car state is unchanged; a new alert is "
        "still announced normally. Use this when the user says they have seen it or want it silenced.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            CarMonitor* monitor = CarMonitor::GetInstance();
            if (monitor == nullptr) {
                return std::string("{\"success\": false, \"message\": \"car monitor not initialized\"}");
            }
            // 无活跃告警时 car_monitor 里的确认是空操作，如实回报避免模型虚构"已静音"
            if (monitor->GetActiveAlert() == ALERT_NONE) {
                return std::string("{\"success\": false, \"message\": \"there is no active alert to confirm\"}");
            }
            CarCommand cmd;
            cmd.type = CMD_CONFIRM_ALERT;
            cmd.param = 0;
            // 直接走命令队列而非 HandleConfirmAlertButton()，以便把"队列满"如实回报给模型
            if (!monitor->SubmitCommand(cmd)) {
                return std::string("{\"success\": false, \"message\": \"command queue busy, please retry\"}");
            }
            ESP_LOGI(TAG, "MCP 确认告警命令已提交");
            return std::string("{\"success\": true, \"message\": \"alert confirmed, local alarm muted for 30 seconds\"}");
        });

    // ---- 5) 切换车辆状态：行驶中 ⇄ 停车 ----
    // 演示入口：本板 GPIO11 被 SW3 按键占用（切屏），拼不出"切换车辆状态"的物理按键；
    // 摄像头页那颗屏幕按钮也已移除。所以车辆状态目前只能由命令切换（语音或将来恢复的按钮）。
    mcp.AddTool("self.car.toggle_vehicle_state",
        "Toggle the (simulated) vehicle state between RUNNING and PARKED. Use this when the user "
        "asks to switch the car to parked or to running, e.g. to start the person-left reminder test. "
        "The command is applied asynchronously, so call self.car.get_status afterwards to read the "
        "resulting vehicle_state.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            CarMonitor* monitor = CarMonitor::GetInstance();
            if (monitor == nullptr) {
                return std::string("{\"success\": false, \"message\": \"car monitor not initialized\"}");
            }
            CarCommand cmd;
            cmd.type = CMD_TOGGLE_VEHICLE_STATE;
            cmd.param = 0;
            // 与 HandleUserButtonPress() 同一条命令路径；这里直接发队列是为了能如实回报"队列满"
            if (!monitor->SubmitCommand(cmd)) {
                return std::string("{\"success\": false, \"message\": \"command queue busy, please retry\"}");
            }
            ESP_LOGI(TAG, "MCP 车辆状态切换命令已提交");
            return std::string("{\"success\": true, \"message\": \"vehicle state toggle submitted; "
                               "call self.car.get_status to read the new vehicle_state\"}");
        });
}
