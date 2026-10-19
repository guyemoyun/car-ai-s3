/**
 * @file car_ui_helper.cc
 * @brief 车载监测界面辅助函数实现（基于 ScreenManager 架构）
 * @details 数据页(CarUI)只显示数据；三颗按钮在摄像头页(CarCameraUI)，
 *          回调统一在本类里接到 CarMonitorHelper。
 */

#include "car_ui_helper.h"
#include "car_monitor.h"
#include "car_monitor_helper.h"
#include "car_person_detect.h"   // CarPersonDetect::SetResultSink()
#include "screen_manager.h"
#include "board.h"               // Board::GetInstance()
#include "camera.h"              // Camera::GetPreviewFrame()
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

#define TAG "CarUIHelper"

// 预览帧缓冲：320x240 RGB565 = 153,600 字节（正好等于摄像头 QVGA 原始画幅，1:1 显示）。
// 放 PSRAM 即可：LVGL 会先把它拷进自己的【内部 DMA】绘制缓冲再推屏，
// 不存在"SPI 直接从 PSRAM 取数"的问题（SPI3 不支持外部内存 DMA，会要求等大内部中转）。
// 双缓冲乒乓：LVGL 渲染时读一块，任务写另一块，避免读到半新半旧的画面。
#define CAM_PREVIEW_W     320
#define CAM_PREVIEW_H     240
#define CAM_PREVIEW_BYTES (CAM_PREVIEW_W * CAM_PREVIEW_H * 2)

// 静态成员初始化
TaskHandle_t CarUIHelper::update_task_ = nullptr;
TaskHandle_t CarUIHelper::camera_preview_task_ = nullptr;

// 双缓冲帧缓冲与图像描述符（文件作用域，生命周期覆盖整个运行期）
static uint8_t* s_cam_preview_buf[2] = {nullptr, nullptr};
static lv_img_dsc_t s_cam_preview_dsc[2];
static int s_cam_preview_show = 0;  // 当前正在显示的缓冲索引

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

    // 三颗屏幕按键（切换状态/确认告警/测试告警）已暂时移除：触摸尚不稳定。
    // 回调仍照旧挂上，日后恢复按键控件时无需改动这里。
    camera_ui->SetToggleVehicleCallback(OnToggleVehicle);
    camera_ui->SetConfirmAlertCallback(OnConfirmAlert);
    camera_ui->SetTestAlertCallback(OnTestAlert);

    // 检测框叠加出口（M4）：人员检测任务出结果时把框坐标交给摄像头页绘框。
    // 必须在 CarPersonDetect::Start() 之前注册，否则首次检测的框会丢掉。
    CarPersonDetect::SetResultSink(OnPersonDetectResult);

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

    // 分配双缓冲（PSRAM），只在首次分配
    if (s_cam_preview_buf[0] == nullptr) {
        for (int i = 0; i < 2; i++) {
            s_cam_preview_buf[i] = (uint8_t*)heap_caps_malloc(CAM_PREVIEW_BYTES, MALLOC_CAP_SPIRAM);
            if (s_cam_preview_buf[i] == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate preview buffer %d (%d bytes) in PSRAM",
                         i, CAM_PREVIEW_BYTES);
                for (int j = 0; j < i; j++) {
                    heap_caps_free(s_cam_preview_buf[j]);
                    s_cam_preview_buf[j] = nullptr;
                }
                return false;
            }
            memset(&s_cam_preview_dsc[i], 0, sizeof(s_cam_preview_dsc[i]));
            s_cam_preview_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
            s_cam_preview_dsc[i].header.cf = LV_COLOR_FORMAT_RGB565;
            s_cam_preview_dsc[i].header.w = CAM_PREVIEW_W;
            s_cam_preview_dsc[i].header.h = CAM_PREVIEW_H;
            s_cam_preview_dsc[i].header.stride = CAM_PREVIEW_W * 2;
            s_cam_preview_dsc[i].data_size = CAM_PREVIEW_BYTES;
            s_cam_preview_dsc[i].data = s_cam_preview_buf[i];
        }
        s_cam_preview_show = 0;
        ESP_LOGI(TAG, "Preview double buffer allocated: 2 x %d bytes in PSRAM", CAM_PREVIEW_BYTES);
    }

    // 钉核到 CPU 0、优先级 3：
    // 音频输入与 LVGL 都在 CPU 1；若本任务被调度到 CPU 1，其逐帧 153,600 字节的拷贝
    // 会与 wakenet9 推理抢核，饿死 CPU1 的 IDLE → 触发 task_wdt 并拖垮 AFE 喂数。
    // 钉到 CPU 0 与摄像头采集同核即消除该竞争。
    BaseType_t ret = xTaskCreatePinnedToCore(
        CameraPreviewTaskFunction,
        "cam_preview",
        4096,
        (void*)(uintptr_t)interval_ms,
        3,
        &camera_preview_task_,
        0   // CPU 0
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

    // 性能打点：每满 1 秒汇总一次帧率与各阶段耗时（只在摄像头页打印）
    int64_t stat_window_start_us = esp_timer_get_time();
    uint32_t stat_frames = 0;      // 窗口内成功刷新帧数
    int64_t stat_get_us = 0;       // 累计：取帧+bswap 耗时
    int64_t stat_lock_us = 0;      // 累计：LVGL 上锁+提交 耗时
    int64_t stat_max_frame_us = 0; // 单帧最大总耗时

    while (true) {
        ScreenManager* scr_mgr = ScreenManager::GetInstance();
        const bool on_cam_page = (scr_mgr != nullptr &&
                                  scr_mgr->GetCurrentScreen() == SCREEN_CAR_CAMERA);

        // 只在摄像头页取帧：其它页面不刷，省 CPU 与 SPI 带宽
        if (on_cam_page && s_cam_preview_buf[0] != nullptr) {
            CarCameraUI* camera_ui = scr_mgr->GetCarCameraUI();
            Camera* camera = Board::GetInstance().GetCamera();
            if (camera_ui != nullptr && camera != nullptr) {
                // 乒乓：写"非当前显示"的那块；LVGL 渲染时读的是另一块
                const int next = 1 - s_cam_preview_show;

                int64_t t0 = esp_timer_get_time();
                int w = 0;
                int h = 0;
                bool got = camera->GetPreviewFrame(s_cam_preview_buf[next], CAM_PREVIEW_BYTES, &w, &h);
                int64_t t1 = esp_timer_get_time();

                if (got && w > 0 && h > 0 && (size_t)w * h * 2 <= CAM_PREVIEW_BYTES) {
                    s_cam_preview_dsc[next].header.w = w;
                    s_cam_preview_dsc[next].header.h = h;
                    s_cam_preview_dsc[next].header.stride = w * 2;
                    s_cam_preview_dsc[next].data_size = (size_t)w * h * 2;

                    if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
                        camera_ui->SetFrame(&s_cam_preview_dsc[next]);
                        lvgl_port_unlock();
                    }
                    s_cam_preview_show = next;

                    int64_t t2 = esp_timer_get_time();
                    int64_t frame_us = t2 - t0;
                    stat_get_us += (t1 - t0);      // 取帧 + 字节序转换
                    stat_lock_us += (t2 - t1);     // 提交给 LVGL（含等锁）
                    if (frame_us > stat_max_frame_us) {
                        stat_max_frame_us = frame_us;
                    }
                    stat_frames++;
                }
            }
        }

        // 每满 1 秒汇报一次（避免用 %f：nano 格式化下不可靠，用整数算 fps*10）
        int64_t now = esp_timer_get_time();
        if (on_cam_page && now - stat_window_start_us >= 1000000) {
            int64_t window_us = now - stat_window_start_us;
            if (stat_frames > 0) {
                uint32_t fps_x10 = (uint32_t)((int64_t)stat_frames * 10000000 / window_us);
                ESP_LOGI(TAG, "Preview stats: fps=%u.%u get=%dus lock+draw=%dus max_frame=%dus",
                         fps_x10 / 10, fps_x10 % 10,
                         (int)(stat_get_us / stat_frames),
                         (int)(stat_lock_us / stat_frames),
                         (int)stat_max_frame_us);
            } else {
                ESP_LOGW(TAG, "Preview stats: fps=0.0 (摄像头页未取到帧)");
            }
            stat_window_start_us = now;
            stat_frames = 0;
            stat_get_us = 0;
            stat_lock_us = 0;
            stat_max_frame_us = 0;
        } else if (!on_cam_page) {
            // 离开摄像头页：重置统计窗口，避免把别页的时间算进来
            stat_window_start_us = now;
            stat_frames = 0;
            stat_get_us = 0;
            stat_lock_us = 0;
            stat_max_frame_us = 0;
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

void CarUIHelper::OnPersonDetectResult(int count, const CarDetectionBox* boxes, int n)
{
    // 本回调运行在人员检测任务上下文（CPU0/优先级 1），不在 LVGL 线程，故须自行取锁。
    // 锁超时给短（20ms）：拿不到就丢弃这一帧的框（下一帧 500ms 后再来），
    // 绝不阻塞检测任务——告警判定走 OnPersonDetectionResult，与本回调无关。
    ScreenManager* scr_mgr = ScreenManager::GetInstance();
    if (scr_mgr == nullptr) {
        return;
    }
    CarCameraUI* camera_ui = scr_mgr->GetCarCameraUI();
    if (camera_ui == nullptr) {
        return;
    }
    if (lvgl_port_lock(pdMS_TO_TICKS(20))) {
        camera_ui->SetDetections(count, boxes, n);
        lvgl_port_unlock();
    }
}
