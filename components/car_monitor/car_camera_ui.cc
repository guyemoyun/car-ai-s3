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

// 检测框线宽（像素）。框为空心矩形：只画边框，不填充画面。
#define DETECT_BORDER  2

CarCameraUI::CarCameraUI(lv_obj_t* parent)
    : container_(nullptr),
      preview_area_(nullptr),
      preview_img_(nullptr),
      preview_placeholder_(nullptr),
      overlay_(nullptr),
      detect_boxes_{},
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
    // 子对象随 container_ 一起销毁；显式置空所有指针，避免预览任务(SetFrame)或
    // 人员检测任务的 sink 回调(SetDetections)在本对象销毁后仍访问已释放的 LVGL 对象。
    preview_area_ = nullptr;
    preview_img_ = nullptr;
    preview_placeholder_ = nullptr;
    overlay_ = nullptr;
    for (int i = 0; i < kMaxDetections; i++) {
        detect_boxes_[i] = nullptr;
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

    // ---- 检测框叠加层（M4）：透明容器 + N 个空心矩形，叠在画面之上 ----
    // 尺寸与预览内区一致并居中 => 源坐标 1:1，检测框坐标可直接当摆位用。
    // 创建在 preview_img_ 之后：LVGL 后创建者在上层，故框压在画面上。
    overlay_ = lv_obj_create(preview_area_);
    lv_obj_set_size(overlay_, PREVIEW_INNER_WIDTH, PREVIEW_INNER_HEIGHT);
    lv_obj_align(overlay_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlay_, 0, 0);
    lv_obj_set_style_radius(overlay_, 0, 0);
    lv_obj_set_style_pad_all(overlay_, 0, 0);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_CLICKABLE);  // 只做显示，不吃点击/滚动

    for (int i = 0; i < kMaxDetections; i++) {
        lv_obj_t* box = lv_obj_create(overlay_);
        lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);  // 空心：只留边框
        lv_obj_set_style_border_color(box, COLOR_ALERT, 0);
        lv_obj_set_style_border_width(box, DETECT_BORDER, 0);
        lv_obj_set_style_radius(box, 0, 0);
        lv_obj_set_style_pad_all(box, 0, 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);        // 默认无框
        detect_boxes_[i] = box;
    }
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

void CarCameraUI::SetDetections(int count, const CarDetectionBox* boxes, int n)
{
    if (overlay_ == nullptr) {
        return;
    }
    // count<=0 或没有框 => 全部隐藏（画面只剩预览）
    const int shown = (count > 0 && boxes != nullptr && n > 0) ? n : 0;

    for (int i = 0; i < kMaxDetections; i++) {
        lv_obj_t* box = detect_boxes_[i];
        if (box == nullptr) {
            continue;
        }
        if (i >= shown) {
            lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        // 源坐标与内区 1:1，直接摆位；只做边界夹紧（模型坐标已在 run() 内映射回 320x240）
        int x1 = boxes[i].x1;
        int y1 = boxes[i].y1;
        int x2 = boxes[i].x2;
        int y2 = boxes[i].y2;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 > PREVIEW_INNER_WIDTH)  x2 = PREVIEW_INNER_WIDTH;
        if (y2 > PREVIEW_INNER_HEIGHT) y2 = PREVIEW_INNER_HEIGHT;
        if (x2 - x1 <= 0 || y2 - y1 <= 0) {
            lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_pos(box, x1, y1);
        lv_obj_set_size(box, x2 - x1, y2 - y1);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_HIDDEN);
    }
    // 只失效叠加层：框是叠加层子对象，脏区自动合并到框外接矩形，不会全屏重绘
    lv_obj_invalidate(overlay_);
}
