/**
 * @file car_ui_helper.cc
 * @brief 车载监测界面辅助函数实现
 */

#include "car_ui_helper.h"
#include "car_monitor/car_monitor.h"
#include "car_monitor_helper.h"
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "CarUIHelper"

// 静态成员初始化
CarUI* CarUIHelper::ui_instance_ = nullptr;
TaskHandle_t CarUIHelper::update_task_ = nullptr;

bool CarUIHelper::Initialize(lv_obj_t* parent)
{
    if (ui_instance_ != nullptr) {
        ESP_LOGW(TAG, "CarUI already initialized");
        return true;
    }

    // 创建 UI 实例（必须在 LVGL 锁内创建）
    if (lvgl_port_lock(0)) {
        ui_instance_ = new CarUI(parent);

        // 设置按钮回调
        ui_instance_->SetToggleVehicleCallback(OnToggleVehicle);
        ui_instance_->SetConfirmAlertCallback(OnConfirmAlert);
        ui_instance_->SetTestAlertCallback(OnTestAlert);

        lvgl_port_unlock();

        ESP_LOGI(TAG, "CarUI initialized successfully");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
        return false;
    }
}

bool CarUIHelper::StartUpdateTask(uint32_t update_interval_ms)
{
    if (ui_instance_ == nullptr) {
        ESP_LOGE(TAG, "CarUI not initialized, call Initialize() first");
        return false;
    }

    if (update_task_ != nullptr) {
        ESP_LOGW(TAG, "Update task already running");
        return true;
    }

    // 创建更新任务
    BaseType_t ret = xTaskCreate(
        UpdateTaskFunction,
        "car_ui_update",
        4096,
        (void*)(uintptr_t)update_interval_ms,
        5,
        &update_task_
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create update task");
        return false;
    }

    ESP_LOGI(TAG, "CarUI update task started (interval: %lu ms)", update_interval_ms);
    return true;
}

void CarUIHelper::StopUpdateTask()
{
    if (update_task_ != nullptr) {
        vTaskDelete(update_task_);
        update_task_ = nullptr;
        ESP_LOGI(TAG, "CarUI update task stopped");
    }
}

void CarUIHelper::UpdateUI()
{
    if (ui_instance_ == nullptr) {
        return;
    }

    // 从 CarMonitor 获取最新快照
    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) {
        return;
    }

    CarSnapshot snapshot;
    if (monitor->GetSnapshot(&snapshot)) {
        // 在 LVGL 锁内更新 UI
        if (lvgl_port_lock(0)) {
            ui_instance_->Update(snapshot);
            lvgl_port_unlock();
        }
    }
}

void CarUIHelper::SetVisible(bool visible)
{
    if (ui_instance_ != nullptr) {
        if (lvgl_port_lock(0)) {
            ui_instance_->SetVisible(visible);
            lvgl_port_unlock();
        }
    }
}

CarUI* CarUIHelper::GetInstance()
{
    return ui_instance_;
}

void CarUIHelper::UpdateTaskFunction(void* arg)
{
    uint32_t interval_ms = (uint32_t)(uintptr_t)arg;
    TickType_t last_wake_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "CarUI update task running");

    while (true) {
        // 更新 UI
        UpdateUI();

        // 等待下一个周期
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(interval_ms));
    }
}

void CarUIHelper::OnToggleVehicle()
{
    ESP_LOGI(TAG, "Toggle vehicle button pressed");
    CarMonitorHelper::HandleUserButtonPress();
}

void CarUIHelper::OnConfirmAlert()
{
    ESP_LOGI(TAG, "Confirm alert button pressed");
    CarMonitorHelper::HandleConfirmAlertButton();
}

void CarUIHelper::OnTestAlert()
{
    ESP_LOGI(TAG, "Test alert button pressed");
    CarMonitorHelper::HandleTestAlertButton();
}
