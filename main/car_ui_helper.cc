/**
 * @file car_ui_helper.cc
 * @brief 车载监测界面辅助函数实现（基于 ScreenManager 架构）
 * @details 数据页(CarUI)只显示数据；三颗按钮在摄像头页(CarCameraUI)，
 *          回调统一在本类里接到 CarMonitorHelper。
 */

#include "car_ui_helper.h"
#include "car_monitor.h"
#include "car_monitor_helper.h"
#include "screen_manager.h"
#include "board.h"       // Board::GetInstance()
#include "camera.h"      // Camera::GetPreviewFrame()
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

#define TAG "CarUIHelper"

// 摄像头页预览帧缓冲：640x480 RGB565 = 614,400 字节，**必须放 PSRAM**
// （内部 RAM 稳态仅 ~32KB，放不下；PSRAM 有 8MB，充裕）
#define CAM_PREVIEW_W     640
#define CAM_PREVIEW_H     480
#define CAM_PREVIEW_BYTES (CAM_PREVIEW_W * CAM_PREVIEW_H * 2)

// 静态成员初始化
TaskHandle_t CarUIHelper::update_task_ = nullptr;
TaskHandle_t CarUIHelper::camera_preview_task_ = nullptr;

// 预览帧缓冲与图像描述符（文件作用域，生命周期覆盖整个运行期）
static uint8_t* s_cam_preview_buf = nullptr;
static lv_img_dsc_t s_cam_preview_dsc;

bool CarUIHelper::Initialize()
{
    // 新架构下，UI 由 ScreenManager 统一管理
    ScreenManager* scr_mgr = ScreenManager::GetInstance();
    if (scr_mgr == nullptr) {
        ESP_LOGE(TAG, "ScreenManager not initialized");
        return false;
    }

    if (scr_mgr->GetCarDataUI() == nullptr) {
        ESP_LOGE(TAG, "CarUI (data page) not available from ScreenManager");
        return false;
    }

    CarCameraUI* camera_ui = scr_mgr->GetCarCameraUI();
    if (camera_ui == nullptr) {
        ESP_LOGE(TAG, "CarCameraUI not available from ScreenManager");
        return false;
    }

    // 三颗按钮都在摄像头页，回调挂到该页
    camera_ui->SetToggleVehicleCallback(OnToggleVehicle);
    camera_ui->SetConfirmAlertCallback(OnConfirmAlert);
    camera_ui->SetTestAlertCallback(OnTestAlert);

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

    CarUI* ui = scr_mgr->GetCarDataUI();
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
        scr_mgr->SwitchToScreen(SCREEN_CAR_DATA, true);
        ESP_LOGI(TAG, "Switched to car data screen");
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

void CarUIHelper::UpdateTaskFunction(void* arg)
{
    uint32_t interval_ms = (uint32_t)(uintptr_t)arg;
    TickType_t last_wake_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "CarUI update task running");

    while (true) {
        // 无条件刷新数据页：数据页不可见时更新只是标记脏区（LVGL 不渲染非活动屏幕），
        // 开销可忽略；好处是切到数据页时数值已是最新，不会先看到 500ms 前的旧值。
        UpdateUI();

        // 等待下一个周期
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(interval_ms));
    }
}

bool CarUIHelper::StartCameraPreviewTask(uint32_t interval_ms)
{
    if (camera_preview_task_ != nullptr) {
        ESP_LOGW(TAG, "Camera preview task already running");
        return true;
    }

    // 分配预览缓冲（PSRAM）并初始化图像描述符（只在首次分配）
    if (s_cam_preview_buf == nullptr) {
        s_cam_preview_buf = (uint8_t*)heap_caps_malloc(CAM_PREVIEW_BYTES, MALLOC_CAP_SPIRAM);
        if (s_cam_preview_buf == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes preview buffer in PSRAM", CAM_PREVIEW_BYTES);
            return false;
        }
        memset(&s_cam_preview_dsc, 0, sizeof(s_cam_preview_dsc));
        s_cam_preview_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_cam_preview_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        s_cam_preview_dsc.header.w = CAM_PREVIEW_W;
        s_cam_preview_dsc.header.h = CAM_PREVIEW_H;
        s_cam_preview_dsc.header.stride = CAM_PREVIEW_W * 2;
        s_cam_preview_dsc.data_size = CAM_PREVIEW_BYTES;
        s_cam_preview_dsc.data = s_cam_preview_buf;
    }

    BaseType_t ret = xTaskCreate(
        CameraPreviewTaskFunction,
        "cam_preview",
        4096,
        (void*)(uintptr_t)interval_ms,
        4,
        &camera_preview_task_
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create camera preview task");
        return false;
    }

    ESP_LOGI(TAG, "Camera preview task started (interval: %lu ms)", interval_ms);
    return true;
}

void CarUIHelper::StopCameraPreviewTask()
{
    if (camera_preview_task_ != nullptr) {
        vTaskDelete(camera_preview_task_);
        camera_preview_task_ = nullptr;
        ESP_LOGI(TAG, "Camera preview task stopped");
    }
}

void CarUIHelper::CameraPreviewTaskFunction(void* arg)
{
    uint32_t interval_ms = (uint32_t)(uintptr_t)arg;
    TickType_t last_wake_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "Camera preview task running");

    while (true) {
        ScreenManager* scr_mgr = ScreenManager::GetInstance();
        // 只在摄像头页取帧：其它页面不刷，省 CPU 与 SPI 带宽
        if (scr_mgr != nullptr && scr_mgr->GetCurrentScreen() == SCREEN_CAR_CAMERA &&
            s_cam_preview_buf != nullptr) {
            CarCameraUI* camera_ui = scr_mgr->GetCarCameraUI();
            Camera* camera = Board::GetInstance().GetCamera();
            if (camera_ui != nullptr && camera != nullptr) {
                int w = 0;
                int h = 0;
                if (camera->GetPreviewFrame(s_cam_preview_buf, CAM_PREVIEW_BYTES, &w, &h) &&
                    w > 0 && h > 0 && (size_t)w * h * 2 <= CAM_PREVIEW_BYTES) {
                    if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
                        s_cam_preview_dsc.header.w = w;
                        s_cam_preview_dsc.header.h = h;
                        s_cam_preview_dsc.header.stride = w * 2;
                        s_cam_preview_dsc.data_size = (size_t)w * h * 2;
                        camera_ui->SetFrame(&s_cam_preview_dsc);
                        lvgl_port_unlock();
                    }
                }
            }
        }

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
