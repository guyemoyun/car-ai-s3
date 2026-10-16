/**
 * @file car_camera_ui.cc
 * @brief 车载摄像头页 LVGL 界面实现
 */

#include "car_camera_ui.h"
#include "car_ui_colors.h"
#include <esp_log.h>

#define TAG "CarCameraUI"

// 字体声明（由 78/xiaozhi-fonts 组件提供），必须放在文件作用域
LV_FONT_DECLARE(font_puhui_20_4);

// 预览区尺寸：摄像头 GC0308 输出 640x480，是 4:3 画幅。
// 按 4:3 定尺寸，将来接真实画面不用返工。
// 不能取 320x240：容器内边距16 + 预览240 + 间距8 + 按钮区58 = 322 > 320，会溢出。
// 因此取 300x225（仍是 4:3），合计 16+225+8+58 = 307 <= 320。
#define PREVIEW_WIDTH   300
#define PREVIEW_HEIGHT  225

CarCameraUI::CarCameraUI(lv_obj_t* parent)
    : container_(nullptr),
      preview_area_(nullptr),
      btn_toggle_vehicle_(nullptr),
      btn_confirm_alert_(nullptr),
      btn_test_alert_(nullptr),
      toggle_vehicle_callback_(nullptr),
      confirm_alert_callback_(nullptr),
      test_alert_callback_(nullptr)
{
    // 如果没有指定父对象，使用默认屏幕
    if (parent == nullptr) {
        parent = lv_scr_act();
    }

    // 创建主容器
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container_, COLOR_BACKGROUND, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 8, 0);
    lv_obj_set_style_pad_row(container_, 8, 0);  // flex 纵向流：行间距
    // 整页不滚动：本页只有"预览区 + 按钮行"两块固定内容
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    // 创建界面布局
    CreateLayout();

    ESP_LOGI(TAG, "CarCameraUI initialized");
}

CarCameraUI::~CarCameraUI()
{
    if (container_) {
        lv_obj_del(container_);
        container_ = nullptr;
    }
}

void CarCameraUI::CreateLayout()
{
    // 纵向两块：预览区（上）/ 按钮行（下）
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // ---- 摄像头预览占位区（4:3 居中）----
    preview_area_ = lv_obj_create(container_);
    lv_obj_set_size(preview_area_, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    lv_obj_set_style_bg_color(preview_area_, COLOR_PANEL, 0);
    lv_obj_set_style_border_color(preview_area_, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(preview_area_, 2, 0);
    lv_obj_set_style_radius(preview_area_, 8, 0);
    lv_obj_clear_flag(preview_area_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* placeholder = lv_label_create(preview_area_);
    lv_label_set_text(placeholder, "摄像头预览区域\n（待集成）");
    lv_obj_set_style_text_color(placeholder, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(placeholder, &font_puhui_20_4, 0);
    lv_obj_center(placeholder);

    // ---- 按钮行 ----
    CreateButtonArea(container_);
}

void CarCameraUI::CreateButtonArea(lv_obj_t* parent)
{
    lv_obj_t* btn_panel = lv_obj_create(parent);
    lv_obj_set_size(btn_panel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_panel, 0, 0);
    lv_obj_set_style_pad_all(btn_panel, 4, 0);
    lv_obj_set_style_pad_column(btn_panel, 8, 0);  // flex 横向流：列间距
    lv_obj_clear_flag(btn_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(btn_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 切换车辆状态按钮
    btn_toggle_vehicle_ = lv_btn_create(btn_panel);
    lv_obj_set_size(btn_toggle_vehicle_, 140, 50);
    lv_obj_set_style_bg_color(btn_toggle_vehicle_, COLOR_BUTTON, 0);
    lv_obj_set_style_bg_color(btn_toggle_vehicle_, lv_color_hex(0x1976D2), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn_toggle_vehicle_, 10, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_toggle_vehicle_, 8, 0);  // 圆角，更易触摸
    lv_obj_t* label1 = lv_label_create(btn_toggle_vehicle_);
    lv_label_set_text(label1, "切换状态");
    lv_obj_set_style_text_font(label1, &font_puhui_20_4, 0);
    lv_obj_center(label1);
    lv_obj_add_event_cb(btn_toggle_vehicle_, ButtonEventCallback, LV_EVENT_CLICKED, this);

    // 确认告警按钮
    btn_confirm_alert_ = lv_btn_create(btn_panel);
    lv_obj_set_size(btn_confirm_alert_, 140, 50);
    lv_obj_set_style_bg_color(btn_confirm_alert_, COLOR_RUNNING, 0);
    lv_obj_set_style_bg_color(btn_confirm_alert_, lv_color_hex(0x388E3C), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn_confirm_alert_, 10, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_confirm_alert_, 8, 0);
    lv_obj_t* label2 = lv_label_create(btn_confirm_alert_);
    lv_label_set_text(label2, "确认告警");
    lv_obj_set_style_text_font(label2, &font_puhui_20_4, 0);
    lv_obj_center(label2);
    lv_obj_add_event_cb(btn_confirm_alert_, ButtonEventCallback, LV_EVENT_CLICKED, this);

    // 测试告警按钮
    btn_test_alert_ = lv_btn_create(btn_panel);
    lv_obj_set_size(btn_test_alert_, 140, 50);
    lv_obj_set_style_bg_color(btn_test_alert_, COLOR_BUTTON_ALERT, 0);
    lv_obj_set_style_bg_color(btn_test_alert_, lv_color_hex(0xD32F2F), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn_test_alert_, 10, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_test_alert_, 8, 0);
    lv_obj_t* label3 = lv_label_create(btn_test_alert_);
    lv_label_set_text(label3, "测试告警");
    lv_obj_set_style_text_font(label3, &font_puhui_20_4, 0);
    lv_obj_center(label3);
    lv_obj_add_event_cb(btn_test_alert_, ButtonEventCallback, LV_EVENT_CLICKED, this);
}

void CarCameraUI::SetToggleVehicleCallback(void (*callback)(void))
{
    toggle_vehicle_callback_ = callback;
}

void CarCameraUI::SetConfirmAlertCallback(void (*callback)(void))
{
    confirm_alert_callback_ = callback;
}

void CarCameraUI::SetTestAlertCallback(void (*callback)(void))
{
    test_alert_callback_ = callback;
}

void CarCameraUI::SetVisible(bool visible)
{
    if (container_) {
        if (visible) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void CarCameraUI::ButtonEventCallback(lv_event_t* e)
{
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    CarCameraUI* ui = static_cast<CarCameraUI*>(lv_event_get_user_data(e));

    if (btn == ui->btn_toggle_vehicle_ && ui->toggle_vehicle_callback_) {
        ESP_LOGI(TAG, "Toggle vehicle button clicked");
        ui->toggle_vehicle_callback_();
    } else if (btn == ui->btn_confirm_alert_ && ui->confirm_alert_callback_) {
        ESP_LOGI(TAG, "Confirm alert button clicked");
        ui->confirm_alert_callback_();
    } else if (btn == ui->btn_test_alert_ && ui->test_alert_callback_) {
        ESP_LOGI(TAG, "Test alert button clicked");
        ui->test_alert_callback_();
    }
}
