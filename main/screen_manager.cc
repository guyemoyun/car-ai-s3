/**
 * @file screen_manager.cc
 * @brief 屏幕管理器实现
 */

#include "screen_manager.h"
#include <esp_log.h>
#include <esp_lvgl_port.h>

#define TAG "ScreenManager"

// 静态成员初始化
ScreenManager* ScreenManager::instance_ = nullptr;

ScreenManager::ScreenManager()
    : chat_screen_(nullptr),
      car_monitor_screen_(nullptr),
      car_ui_(nullptr),
      display_(nullptr),
      current_screen_(SCREEN_CHAT),
      screensaver_enabled_(true)
{
}

ScreenManager::~ScreenManager()
{
    if (car_ui_) {
        delete car_ui_;
        car_ui_ = nullptr;
    }

    if (car_monitor_screen_) {
        lv_obj_del(car_monitor_screen_);
        car_monitor_screen_ = nullptr;
    }
}

bool ScreenManager::Initialize(Display* display)
{
    if (instance_ != nullptr) {
        ESP_LOGW(TAG, "ScreenManager already initialized");
        return true;
    }

    instance_ = new ScreenManager();
    instance_->display_ = display;

    // 必须在 LVGL 锁内创建 UI
    if (!lvgl_port_lock(pdMS_TO_TICKS(100))) {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
        delete instance_;
        instance_ = nullptr;
        return false;
    }

    // 1. 保存当前屏幕（聊天屏幕）
    instance_->chat_screen_ = lv_scr_act();
    ESP_LOGI(TAG, "Chat screen saved: %p", instance_->chat_screen_);

    // 2. 创建车载监测屏幕（独立屏幕对象）
    instance_->car_monitor_screen_ = lv_obj_create(NULL);  // NULL = 独立屏幕
    lv_obj_set_style_bg_color(instance_->car_monitor_screen_, lv_color_hex(0x000000), 0);
    ESP_LOGI(TAG, "Car monitor screen created: %p", instance_->car_monitor_screen_);

    // 3. 在车载监测屏幕中创建 CarUI
    instance_->car_ui_ = new CarUI(instance_->car_monitor_screen_);
    ESP_LOGI(TAG, "CarUI created in car monitor screen");

    lvgl_port_unlock();

    ESP_LOGI(TAG, "ScreenManager initialized successfully");
    return true;
}

ScreenManager* ScreenManager::GetInstance()
{
    return instance_;
}

void ScreenManager::SwitchToScreen(ScreenType screen, bool anim)
{
    if (screen < 0 || screen >= SCREEN_COUNT) {
        ESP_LOGE(TAG, "Invalid screen type: %d", screen);
        return;
    }

    if (current_screen_ == screen) {
        ESP_LOGD(TAG, "Already on screen %d", screen);
        return;
    }

    if (!lvgl_port_lock(pdMS_TO_TICKS(100))) {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
        return;
    }

    lv_obj_t* target_screen = nullptr;

    switch (screen) {
        case SCREEN_CHAT:
            target_screen = chat_screen_;
            // 回到聊天屏幕时，允许屏保（已禁用屏保功能）
            // screensaver_enabled_ = true;
            ESP_LOGI(TAG, "Switching to CHAT screen");
            break;

        case SCREEN_CAR_MONITOR:
            target_screen = car_monitor_screen_;
            // 车载监测屏幕时，禁用屏保（已禁用屏保功能）
            // if (display_ != nullptr) {
            //     display_->StopScreensaver();
            // }
            // screensaver_enabled_ = false;
            ESP_LOGI(TAG, "Switching to CAR_MONITOR screen");
            break;

        default:
            lvgl_port_unlock();
            return;
    }

    if (target_screen != nullptr) {
        // 使用 LVGL 的屏幕加载函数切换
        if (anim) {
            lv_scr_load_anim(target_screen, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
        } else {
            lv_scr_load(target_screen);
        }
        current_screen_ = screen;
        ESP_LOGI(TAG, "Screen switched to %d", screen);
    }

    lvgl_port_unlock();
}

void ScreenManager::SwitchToNextScreen()
{
    ScreenType next = (ScreenType)((current_screen_ + 1) % SCREEN_COUNT);
    SwitchToScreen(next, true);
}

ScreenType ScreenManager::GetCurrentScreen()
{
    return current_screen_;
}

CarUI* ScreenManager::GetCarUI()
{
    return car_ui_;
}

void ScreenManager::SetScreensaverEnabled(bool enable)
{
    screensaver_enabled_ = enable;

    if (display_ != nullptr && !enable) {
        display_->StopScreensaver();
        ESP_LOGI(TAG, "Screensaver disabled");
    } else {
        ESP_LOGI(TAG, "Screensaver enabled");
    }
}
