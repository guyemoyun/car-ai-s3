/**
 * @file car_ui.cc
 * @brief 车载数据页 LVGL 界面实现
 */

#include "car_ui.h"
#include "car_ui_colors.h"
#include <esp_log.h>
#include <cstdio>
#include <cstring>

#define TAG "CarUI"

// 字体声明（由 78/xiaozhi-fonts 组件提供）。必须在文件作用域声明，
// 写在函数内部只对该函数生效，其它函数会报 "not declared in this scope"。
LV_FONT_DECLARE(font_puhui_20_4);

CarUI::CarUI(lv_obj_t* parent)
    : container_(nullptr),
      label_vehicle_state_(nullptr),
      label_temperature_(nullptr),
      label_humidity_(nullptr),
      label_light_(nullptr),
      label_person_count_(nullptr),
      label_confidence_(nullptr),
      label_alert_(nullptr)
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
    // 整页不滚动：内容固定三段，塞不下时应压缩内容而不是出滚动条。
    // 这是本次拆页的全部意义——一旦出现滚动条，脏区立刻回到全屏。
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    // 创建界面布局
    CreateLayout();

    ESP_LOGI(TAG, "CarUI (data page) initialized");
}

CarUI::~CarUI()
{
    if (container_) {
        lv_obj_del(container_);
        container_ = nullptr;
    }
}

void CarUI::CreateLayout()
{
    // 纵向三段：顶部通栏 / 中部左右两栏 / 底部通栏
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // ---- 顶部通栏：车辆状态 ----
    lv_obj_t* top = lv_obj_create(container_);
    lv_obj_set_size(top, LV_PCT(100), 52);
    lv_obj_set_style_bg_color(top, COLOR_PANEL, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_radius(top, 8, 0);
    lv_obj_set_style_pad_all(top, 10, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    label_vehicle_state_ = lv_label_create(top);
    lv_label_set_text(label_vehicle_state_, "🚗 车辆: 未知");
    lv_obj_set_style_text_color(label_vehicle_state_, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_vehicle_state_, &font_puhui_20_4, 0);

    // ---- 中部：左右两栏（用 flex_grow 吃掉剩余高度，横向 480 宽度充分利用）----
    lv_obj_t* middle = lv_obj_create(container_);
    lv_obj_set_size(middle, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(middle, 1);
    lv_obj_set_style_bg_opa(middle, LV_OPA_TRANSP, 0);  // 透明：两栏各自有面板底色
    lv_obj_set_style_border_width(middle, 0, 0);
    lv_obj_set_style_pad_all(middle, 0, 0);
    lv_obj_set_style_pad_column(middle, 8, 0);  // flex 横向流：列间距
    lv_obj_clear_flag(middle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(middle, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(middle, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    CreateEnvPanel(middle);      // 左栏
    CreatePersonPanel(middle);   // 右栏

    // ---- 底部通栏：告警 ----
    lv_obj_t* bottom = lv_obj_create(container_);
    lv_obj_set_size(bottom, LV_PCT(100), 48);
    lv_obj_set_style_bg_color(bottom, COLOR_PANEL, 0);
    lv_obj_set_style_border_width(bottom, 0, 0);
    lv_obj_set_style_radius(bottom, 8, 0);
    lv_obj_set_style_pad_all(bottom, 10, 0);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    label_alert_ = lv_label_create(bottom);
    lv_label_set_text(label_alert_, "⚠️ 告警: 无");
    lv_obj_set_style_text_color(label_alert_, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_alert_, &font_puhui_20_4, 0);
}

void CarUI::CreateEnvPanel(lv_obj_t* parent)
{
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(48), LV_PCT(100));
    lv_obj_set_style_bg_color(panel, COLOR_PANEL, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "📊 环境数据");
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &font_puhui_20_4, 0);

    label_temperature_ = lv_label_create(panel);
    lv_label_set_text(label_temperature_, "  🌡️  温度: --°C");
    lv_obj_set_style_text_color(label_temperature_, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_temperature_, &font_puhui_20_4, 0);

    label_humidity_ = lv_label_create(panel);
    lv_label_set_text(label_humidity_, "  💧 湿度: --%");
    lv_obj_set_style_text_color(label_humidity_, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_humidity_, &font_puhui_20_4, 0);

    label_light_ = lv_label_create(panel);
    lv_label_set_text(label_light_, "  ☀️  光照: --%");
    lv_obj_set_style_text_color(label_light_, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_light_, &font_puhui_20_4, 0);
}

void CarUI::CreatePersonPanel(lv_obj_t* parent)
{
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(48), LV_PCT(100));
    lv_obj_set_style_bg_color(panel, COLOR_PANEL, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "👤 人员检测");
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &font_puhui_20_4, 0);

    label_person_count_ = lv_label_create(panel);
    lv_label_set_text(label_person_count_, "  检测人数: 0");
    lv_obj_set_style_text_color(label_person_count_, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_person_count_, &font_puhui_20_4, 0);

    label_confidence_ = lv_label_create(panel);
    lv_label_set_text(label_confidence_, "  置信度: 0%");
    lv_obj_set_style_text_color(label_confidence_, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_confidence_, &font_puhui_20_4, 0);
}

// 文本去重刷新：LVGL 9.3 的 lv_label_set_text 对相同文本也会重新布局并触发重绘，
// 数值未变时无意义的刷新会造成额外渲染帧。格式化后再比较，天然规避浮点舍入抖动。
static void SetLabelTextIfChanged(lv_obj_t* label, const char* text)
{
    if (label == nullptr || text == nullptr) {
        return;
    }
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

void CarUI::Update(const CarSnapshot& snapshot)
{
    // 更新车辆状态
    const char* state_str = VehicleStateToString(snapshot.vehicle_state);
    char buf[64];
    snprintf(buf, sizeof(buf), "🚗 车辆: %s", state_str);
    SetLabelTextIfChanged(label_vehicle_state_, buf);

    // 根据状态改变颜色
    if (snapshot.vehicle_state == CAR_STATE_RUNNING) {
        lv_obj_set_style_text_color(label_vehicle_state_, COLOR_RUNNING, 0);
    } else if (snapshot.vehicle_state == CAR_STATE_PARKED) {
        lv_obj_set_style_text_color(label_vehicle_state_, COLOR_PARKED, 0);
    } else {
        lv_obj_set_style_text_color(label_vehicle_state_, COLOR_TEXT_DIM, 0);
    }

    // 更新环境数据
    snprintf(buf, sizeof(buf), "  🌡️  温度: %.1f°C", snapshot.env_data.temperature);
    SetLabelTextIfChanged(label_temperature_, buf);

    snprintf(buf, sizeof(buf), "  💧 湿度: %.1f%%", snapshot.env_data.humidity);
    SetLabelTextIfChanged(label_humidity_, buf);

    snprintf(buf, sizeof(buf), "  ☀️  光照: %.1f%%", snapshot.env_data.light_percent);
    SetLabelTextIfChanged(label_light_, buf);

    // 更新人员检测数据
    snprintf(buf, sizeof(buf), "  检测人数: %d", snapshot.person_data.person_count);
    SetLabelTextIfChanged(label_person_count_, buf);

    snprintf(buf, sizeof(buf), "  置信度: %.0f%%", snapshot.person_data.confidence * 100.0f);
    SetLabelTextIfChanged(label_confidence_, buf);

    // 更新告警状态
    const char* alert_str = AlertToString(snapshot.active_alert);
    snprintf(buf, sizeof(buf), "⚠️ 告警: %s", alert_str);
    SetLabelTextIfChanged(label_alert_, buf);

    // 如果有活跃告警，改变颜色
    if (snapshot.active_alert != ALERT_NONE) {
        lv_obj_set_style_text_color(label_alert_, COLOR_ALERT, 0);
    } else {
        lv_obj_set_style_text_color(label_alert_, COLOR_TEXT, 0);
    }
}

void CarUI::SetVisible(bool visible)
{
    if (container_) {
        if (visible) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

const char* CarUI::AlertToString(AlertType alert)
{
    switch (alert) {
        case ALERT_NONE:
            return "无";
        case ALERT_PERSON_LEFT:
            return "人员遗留";
        case ALERT_TEMP_HIGH:
            return "高温";
        case ALERT_LIGHT_ABNORMAL:
            return "光线异常";
        default:
            return "未知";
    }
}

const char* CarUI::VehicleStateToString(CarVehicleState state)
{
    switch (state) {
        case CAR_STATE_UNKNOWN:
            return "未知";
        case CAR_STATE_RUNNING:
            return "行驶中";
        case CAR_STATE_PARKED:
            return "已停车";
        default:
            return "错误";
    }
}
