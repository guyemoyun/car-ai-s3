/**
 * @file car_monitor_helper.cc
 * @brief 车载监测系统集成辅助函数实现
 */

#include "car_monitor_helper.h"

static const char* TAG = "CarHelper";

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
 * @brief 处理用户按键（GPIO11）- 切换车辆启停状态
 * @details 当用户按下GPIO11按键时调用此函数
 * @note 行驶中→停车，停车→行驶中
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
