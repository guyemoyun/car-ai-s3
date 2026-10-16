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
      car_data_screen_(nullptr),
      car_camera_screen_(nullptr),
      car_data_ui_(nullptr),
      car_camera_ui_(nullptr),
      display_(nullptr),
      current_screen_(SCREEN_CHAT)
{
}

ScreenManager::~ScreenManager()
{
    if (car_data_ui_) {
        delete car_data_ui_;
        car_data_ui_ = nullptr;
    }

    if (car_camera_ui_) {
        delete car_camera_ui_;
        car_camera_ui_ = nullptr;
    }

    if (car_data_screen_) {
        lv_obj_del(car_data_screen_);
        car_data_screen_ = nullptr;
    }

    if (car_camera_screen_) {
        lv_obj_del(car_camera_screen_);
        car_camera_screen_ = nullptr;
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

    // 2. 创建车载数据页屏幕（独立屏幕对象）
    instance_->car_data_screen_ = lv_obj_create(NULL);  // NULL = 独立屏幕
    lv_obj_set_style_bg_color(instance_->car_data_screen_, lv_color_hex(0x000000), 0);
    ESP_LOGI(TAG, "Car data screen created: %p", instance_->car_data_screen_);

    // 3. 创建车载摄像头页屏幕（独立屏幕对象）
    instance_->car_camera_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(instance_->car_camera_screen_, lv_color_hex(0x000000), 0);
    ESP_LOGI(TAG, "Car camera screen created: %p", instance_->car_camera_screen_);

    // 4. 在两个屏幕中分别创建各自的界面
    instance_->car_data_ui_ = new CarUI(instance_->car_data_screen_);
    instance_->car_camera_ui_ = new CarCameraUI(instance_->car_camera_screen_);
    ESP_LOGI(TAG, "CarUI (data) + CarCameraUI created");

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
            ESP_LOGI(TAG, "Switching to CHAT screen");
            break;

        case SCREEN_CAR_DATA:
            target_screen = car_data_screen_;
            ESP_LOGI(TAG, "Switching to CAR_DATA screen");
            break;

        case SCREEN_CAR_CAMERA:
            target_screen = car_camera_screen_;
            ESP_LOGI(TAG, "Switching to CAR_CAMERA screen");
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

CarUI* ScreenManager::GetCarDataUI()
{
    return car_data_ui_;
}

CarCameraUI* ScreenManager::GetCarCameraUI()
{
    return car_camera_ui_;
}
