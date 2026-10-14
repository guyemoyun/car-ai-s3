/**
 * @file app_ui_manager.cc
 * @brief 应用程序 UI 管理器实现
 */

#include "app_ui_manager.h"
#include "car_ui_helper.h"
#include <esp_log.h>
#include <esp_lvgl_port.h>

#define TAG "AppUIManager"

// 静态成员初始化
AppUIManager* AppUIManager::instance_ = nullptr;

AppUIManager::AppUIManager()
    : page_manager_(nullptr),
      car_ui_(nullptr),
      display_(nullptr),
      toast_label_(nullptr),
      screensaver_enabled_(true)
{
}

AppUIManager::~AppUIManager()
{
    if (car_ui_) {
        delete car_ui_;
        car_ui_ = nullptr;
    }

    if (page_manager_) {
        delete page_manager_;
        page_manager_ = nullptr;
    }
}

bool AppUIManager::Initialize(Display* display)
{
    if (instance_ != nullptr) {
        ESP_LOGW(TAG, "AppUIManager already initialized");
        return true;
    }

    instance_ = new AppUIManager();
    instance_->display_ = display;

    // 必须在 LVGL 锁内创建 UI
    if (!lvgl_port_lock(pdMS_TO_TICKS(100))) {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
        delete instance_;
        instance_ = nullptr;
        return false;
    }

    // 1. 创建页面管理器
    instance_->page_manager_ = new PageManager(nullptr);
    ESP_LOGI(TAG, "PageManager created");

    // 2. 获取车载监测页面容器
    lv_obj_t* car_page = instance_->page_manager_->GetPageContainer(PAGE_CAR_MONITOR);
    if (car_page == nullptr) {
        ESP_LOGE(TAG, "Failed to get car monitor page container");
        lvgl_port_unlock();
        delete instance_;
        instance_ = nullptr;
        return false;
    }

    // 3. 在车载监测页面中创建 CarUI
    instance_->car_ui_ = new CarUI(car_page);
    ESP_LOGI(TAG, "CarUI created in car monitor page");

    // 4. 创建 Toast 提示标签（全局浮动）
    instance_->toast_label_ = lv_label_create(lv_scr_act());
    lv_obj_set_style_bg_color(instance_->toast_label_, lv_color_hex(0x323232), 0);
    lv_obj_set_style_bg_opa(instance_->toast_label_, LV_OPA_90, 0);
    lv_obj_set_style_text_color(instance_->toast_label_, lv_color_white(), 0);
    lv_obj_set_style_pad_all(instance_->toast_label_, 15, 0);
    lv_obj_set_style_radius(instance_->toast_label_, 8, 0);
    lv_obj_add_flag(instance_->toast_label_, LV_OBJ_FLAG_HIDDEN); // 默认隐藏
    lv_obj_set_pos(instance_->toast_label_, 0, 0); // 位置会在 ShowToast 中动态设置

    lvgl_port_unlock();

    // 5. 默认显示聊天页面（不禁用屏保）
    instance_->SwitchToPage(PAGE_CHAT, false);

    ESP_LOGI(TAG, "AppUIManager initialized successfully");
    return true;
}

AppUIManager* AppUIManager::GetInstance()
{
    return instance_;
}

void AppUIManager::SwitchToPage(PageType page, bool disable_screensaver)
{
    if (page_manager_ == nullptr) {
        ESP_LOGE(TAG, "PageManager not initialized");
        return;
    }

    if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
        // 切换页面
        page_manager_->SwitchToPage(page, true); // true = 使用动画

        // 控制屏保
        if (disable_screensaver && display_ != nullptr) {
            // 停止屏保（如果正在运行）
            display_->StopScreensaver();
            screensaver_enabled_ = false;
            ESP_LOGI(TAG, "Screensaver disabled for page %d", page);
        } else if (page == PAGE_CHAT && display_ != nullptr) {
            // 回到聊天页面时，重新允许屏保
            screensaver_enabled_ = true;
            ESP_LOGI(TAG, "Screensaver re-enabled for chat page");
        }

        lvgl_port_unlock();

        ESP_LOGI(TAG, "Switched to page %d", page);
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
    }
}

PageType AppUIManager::GetCurrentPage()
{
    if (page_manager_ == nullptr) {
        return PAGE_CHAT;
    }
    return page_manager_->GetCurrentPage();
}

lv_obj_t* AppUIManager::GetPageContainer(PageType page)
{
    if (page_manager_ == nullptr) {
        return nullptr;
    }
    return page_manager_->GetPageContainer(page);
}

void AppUIManager::SetScreensaverEnabled(bool enable)
{
    screensaver_enabled_ = enable;

    if (display_ != nullptr) {
        if (enable) {
            ESP_LOGI(TAG, "Screensaver enabled");
        } else {
            display_->StopScreensaver();
            ESP_LOGI(TAG, "Screensaver disabled and stopped");
        }
    }
}

CarUI* AppUIManager::GetCarUI()
{
    return car_ui_;
}

void AppUIManager::ShowButtonFeedback(lv_obj_t* button)
{
    if (button == nullptr) {
        return;
    }

    // 创建按压动画：缩小 -> 恢复
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, button);
    lv_anim_set_time(&anim, 150); // 150ms 动画

    // 缩小到 95%
    lv_anim_set_exec_cb(&anim, [](void* var, int32_t value) {
        lv_obj_t* obj = (lv_obj_t*)var;
        lv_obj_set_style_transform_zoom(obj, value, 0);
    });
    lv_anim_set_values(&anim, 256, 243); // 256 = 100%, 243 = 95%
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&anim, 1); // 来回一次
    lv_anim_set_playback_time(&anim, 150);

    lv_anim_start(&anim);
}

void AppUIManager::ShowToast(const char* message, uint32_t duration_ms)
{
    if (toast_label_ == nullptr || message == nullptr) {
        return;
    }

    if (!lvgl_port_lock(pdMS_TO_TICKS(100))) {
        return;
    }

    // 设置文本
    lv_label_set_text(toast_label_, message);

    // 居中显示（屏幕底部上方 50px）
    lv_obj_update_layout(toast_label_);
    lv_coord_t width = lv_obj_get_width(toast_label_);
    lv_coord_t height = lv_obj_get_height(toast_label_);
    lv_obj_set_pos(toast_label_,
                   (LV_HOR_RES - width) / 2,
                   LV_VER_RES - height - 50);

    // 显示
    lv_obj_clear_flag(toast_label_, LV_OBJ_FLAG_HIDDEN);

    // 创建定时器自动隐藏
    lv_timer_t* timer = lv_timer_create(ToastTimerCallback, duration_ms, toast_label_);
    lv_timer_set_repeat_count(timer, 1);

    lvgl_port_unlock();
}

void AppUIManager::ToastTimerCallback(lv_timer_t* timer)
{
    lv_obj_t* label = (lv_obj_t*)lv_timer_get_user_data(timer);
    if (label) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}
