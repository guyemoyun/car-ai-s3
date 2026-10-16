/**
 * @file car_ui_colors.h
 * @brief 车载两个页面共用的配色
 * @details 数据页(CarUI)与摄像头页(CarCameraUI)共用同一套深色配色。
 *          集中定义在一处，避免两页配色各自漂移导致外观不一致。
 */

#pragma once

#include <lvgl.h>

#define COLOR_BACKGROUND    lv_color_hex(0x1E1E1E)  // 页面背景（深灰）
#define COLOR_PANEL         lv_color_hex(0x2A2A2A)  // 面板背景
#define COLOR_TEXT          lv_color_hex(0xFFFFFF)  // 主文字
#define COLOR_TEXT_DIM      lv_color_hex(0xAAAAAA)  // 次要文字/分隔线
#define COLOR_RUNNING       lv_color_hex(0x4CAF50)  // 绿色（行驶中/确认告警按钮）
#define COLOR_PARKED        lv_color_hex(0xFF9800)  // 橙色（停车）
#define COLOR_ALERT         lv_color_hex(0xF44336)  // 红色（告警）
#define COLOR_BUTTON        lv_color_hex(0x2196F3)  // 蓝色按钮
#define COLOR_BUTTON_ALERT  lv_color_hex(0xFF5722)  // 橙红色按钮
