/**
 * @file car_ui_helper.cc
 * @brief 车载监测界面辅助函数实现（基于 ScreenManager 架构）
 */

#include "car_ui_helper.h"
#include "car_monitor.h"
#include "car_monitor_helper.h"
#include "screen_manager.h"
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "CarUIHelper"

// 静态成员初始化
TaskHandle_t CarUIHelper::update_task_ = nullptr;

bool CarUIHelper::Initialize()
{
    // 新架构下，UI 由 ScreenManager 统一管理
    ScreenManager* scr_mgr = ScreenManager::GetInstance();
    if (scr_mgr == nullptr) {
        ESP_LOGE(TAG, "ScreenManager not initialized");
        return false;
    }

    CarUI* ui = scr_mgr->GetCarUI();
    if (ui == nullptr) {
        ESP_LOGE(TAG, "CarUI not available from ScreenManager");
        return false;
    }

    // 设置按钮回调
    ui->SetToggleVehicleCallback(OnToggleVehicle);
    ui->SetConfirmAlertCallback(OnConfirmAlert);
    ui->SetTestAlertCallback(OnTestAlert);

    ESP_LOGI(TAG, "CarUIHelper initialized successfully");
    return true;
}

bool CarUIHelper::StartUpdateTask(uint32_t update_interval_ms)
{
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
    ScreenManager* scr_mgr = ScreenManager::GetInstance();
    if (scr_mgr == nullptr) {
        return;
    }

    CarUI* ui = scr_mgr->GetCarUI();
    if (ui == nullptr) {
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
        if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
            ui->Update(snapshot);
            lvgl_port_unlock();
        }
    }
}

void CarUIHelper::SwitchToCarMonitorScreen()
{
    ScreenManager* scr_mgr = ScreenManager::GetInstance();
    if (scr_mgr != nullptr) {
        scr_mgr->SwitchToScreen(SCREEN_CAR_MONITOR, true);
        ESP_LOGI(TAG, "Switched to car monitor screen");
    }
}

void CarUIHelper::SwitchToChatScreen()
{
    ScreenManager* scr_mgr = ScreenManager::GetInstance();
    if (scr_mgr != nullptr) {
        scr_mgr->SwitchToScreen(SCREEN_CHAT, true);
        ESP_LOGI(TAG, "Switched to chat screen");
    }
}

void CarUIHelper::ToggleScreen()
{
    ScreenManager* scr_mgr = ScreenManager::GetInstance();
    if (scr_mgr != nullptr) {
        scr_mgr->SwitchToNextScreen();
        ESP_LOGI(TAG, "Toggled to next screen");
    }
}

CarUI* CarUIHelper::GetInstance()
{
    ScreenManager* scr_mgr = ScreenManager::GetInstance();
    if (scr_mgr == nullptr) {
        return nullptr;
    }
    return scr_mgr->GetCarUI();
}

void CarUIHelper::UpdateTaskFunction(void* arg)
{
    uint32_t interval_ms = (uint32_t)(uintptr_t)arg;
    TickType_t last_wake_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "CarUI update task running");

    while (true) {
        // 只在车载监测屏幕时更新 UI（节省 CPU）
        ScreenManager* scr_mgr = ScreenManager::GetInstance();
        if (scr_mgr != nullptr && scr_mgr->GetCurrentScreen() == SCREEN_CAR_MONITOR) {
            UpdateUI();
        }

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
