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

// 预览内区取 320x240 —— 正好等于摄像头 QVGA 原始画幅（GC0308 配 FRAMESIZE_QVGA），
// 源与内区同尺寸 => LVGL 按 1:1 blit，不做软件缩放（比原来 640->296 缩放更省、更清晰）。
// 备注：曾试过照搬同板例程「绕过 LVGL、一次 draw_bitmap 直接推 PSRAM 帧」，但本应用 LCD
// 挂在 SPI3 上，而 SPI3 不支持外部内存 DMA，从 PSRAM 推屏必须经等大的内部 RAM 中转
// （153,600 字节），本应用内部 RAM 不足 → 稳定失败，故回退到 LVGL 路径。
// 外框比内区各大 2px 边框 => 对象 324x244；纵向 4 + 244 + 4 + 58 + 4 = 314 <= 320。
#define PREVIEW_BORDER  2
#define PREVIEW_WIDTH   324
#define PREVIEW_HEIGHT  244
#define PREVIEW_INNER_WIDTH   (PREVIEW_WIDTH  - PREVIEW_BORDER * 2)   // 320
#define PREVIEW_INNER_HEIGHT  (PREVIEW_HEIGHT - PREVIEW_BORDER * 2)   // 240

CarCameraUI::CarCameraUI(lv_obj_t* parent)
    : container_(nullptr),
      preview_area_(nullptr),
      preview_img_(nullptr),
      preview_placeholder_(nullptr),
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
    lv_obj_set_style_pad_all(container_, 4, 0);
    lv_obj_set_style_pad_row(container_, 4, 0);  // flex 纵向流：行间距
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
    // 本页当前只放预览区：三颗 LVGL 按键已暂时移除（见类注释），故纵向居中
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // ---- 摄像头预览区（4:3，画面由预览任务直接推屏）----
    preview_area_ = lv_obj_create(container_);
    lv_obj_set_size(preview_area_, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    lv_obj_set_style_bg_color(preview_area_, COLOR_PANEL, 0);
    lv_obj_set_style_border_color(preview_area_, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_border_width(preview_area_, PREVIEW_BORDER, 0);
    lv_obj_set_style_radius(preview_area_, 0, 0);   // 不圆角：画面与边框直角对齐
    lv_obj_set_style_pad_all(preview_area_, 0, 0);  // 内区不留边，正好 320x240
    lv_obj_clear_flag(preview_area_, LV_OBJ_FLAG_SCROLLABLE);

    // 无帧时的占位文字（SetFrame() 收到首帧后隐藏）
    preview_placeholder_ = lv_label_create(preview_area_);
    lv_label_set_text(preview_placeholder_, "摄像头预览区域\n（等待画面）");
    lv_obj_set_style_text_color(preview_placeholder_, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(preview_placeholder_, &font_puhui_20_4, 0);
    lv_obj_center(preview_placeholder_);

    // 实时画面控件：初始隐藏，由预览任务 SetFrame() 喂帧。
    // 源 320x240 与内区同尺寸 => 1:1 blit，不做软件缩放（省钱且清晰）。
    preview_img_ = lv_image_create(preview_area_);
    lv_obj_set_size(preview_img_, PREVIEW_INNER_WIDTH, PREVIEW_INNER_HEIGHT);
    lv_obj_align(preview_img_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_img_, LV_OBJ_FLAG_HIDDEN);
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

void CarCameraUI::SetFrame(const lv_img_dsc_t* dsc)
{
    if (preview_img_ == nullptr || dsc == nullptr ||
        dsc->header.w == 0 || dsc->header.h == 0 || dsc->data == nullptr) {
        return;
    }
    // 源与画幅内区同为 320x240 -> 1:1，不做缩放（lv_image 默认 scale 即 256=1.0）
    lv_image_set_src(preview_img_, dsc);
    // 首帧到达：隐藏占位文字、显示画面
    if (preview_placeholder_ != nullptr) {
        lv_obj_add_flag(preview_placeholder_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(preview_img_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(preview_img_);
}
