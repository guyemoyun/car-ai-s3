/**
 * @file car_monitor_test_task.cc
 * @brief 车载监测系统测试任务
 * @details 用于验证car_monitor功能是否正常工作
 */

#include "car_monitor_helper.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static const char* TAG = "CarMonitorTest";

/**
 * @brief 车载监测测试任务函数
 * @param arg 任务参数（未使用）
 * @details
 * 此任务每5秒执行一次以下操作：
 * 1. 查询并打印当前监测状态
 * 2. 检查是否有活跃告警
 * 3. 显示环境数据和人员检测结果
 */
void car_monitor_test_task(void* arg) {
    ESP_LOGI(TAG, "车载监测测试任务已启动");

    // 等待2秒让系统初始化完成
    vTaskDelay(pdMS_TO_TICKS(2000));

    uint32_t loop_count = 0;

    while (true) {
        // 每5秒查询一次
        vTaskDelay(pdMS_TO_TICKS(5000));

        loop_count++;
        ESP_LOGI(TAG, "\n========== 车载监测状态查询 #%ld ==========", loop_count);

        // 使用辅助函数打印完整状态
        CarMonitorHelper::PrintStatus();

        // 单独获取环境数据示例
        float temp, humidity, light;
        if (CarMonitorHelper::GetEnvironmentData(&temp, &humidity, &light)) {
            ESP_LOGI(TAG, "环境数据快速查询: 温度=%.1f°C, 湿度=%.1f%%, 光照=%.1f%%",
                     temp, humidity, light);
        }

        // 检查告警
        AlertType alert = CarMonitorHelper::CheckActiveAlert();
        if (alert != ALERT_NONE) {
            ESP_LOGW(TAG, "⚠️ 检测到活跃告警，类型=%d", (int)alert);
        }

        // 每30秒（6次循环）模拟一次按键切换车辆状态
        if (loop_count % 6 == 0) {
            ESP_LOGI(TAG, ">>> 模拟用户按键：切换车辆状态");
            CarMonitorHelper::HandleUserButtonPress();
        }

        // 每60秒（12次循环）模拟一次人员检测结果
        if (loop_count % 12 == 5) {
            ESP_LOGI(TAG, ">>> 模拟人员检测：检测到1人");
            CarMonitorHelper::OnPersonDetectionResult(1, 0.95f);
        } else if (loop_count % 12 == 10) {
            ESP_LOGI(TAG, ">>> 模拟人员检测：无人");
            CarMonitorHelper::OnPersonDetectionResult(0, 0.0f);
        }

        ESP_LOGI(TAG, "==========================================\n");
    }
}

/**
 * @brief 启动车载监测测试任务
 * @return true=任务创建成功，false=任务创建失败
 * @details 创建一个FreeRTOS任务用于测试car_monitor功能
 */
bool start_car_monitor_test_task() {
    BaseType_t ret = xTaskCreate(
        car_monitor_test_task,      // 任务函数
        "car_monitor_test",         // 任务名称
        4096,                       // 栈大小（4KB）
        NULL,                       // 任务参数
        4,                          // 任务优先级
        NULL                        // 任务句柄
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建车载监测测试任务失败");
        return false;
    }

    ESP_LOGI(TAG, "车载监测测试任务创建成功");
    return true;
}
