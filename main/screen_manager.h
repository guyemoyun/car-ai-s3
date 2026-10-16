/**
 * @file screen_manager.h
 * @brief 屏幕管理器 - 管理聊天屏幕与两个车载页面的切换
 */

#pragma once

#include "car_ui.h"
#include "car_camera_ui.h"
#include "display/display.h"
#include <lvgl.h>

/**
 * @enum ScreenType
 * @brief 屏幕类型
 * @details 枚举顺序即 SW3 循环切换的顺序：聊天 → 数据 → 摄像头 → 聊天
 */
typedef enum {
    SCREEN_CHAT = 0,        ///< 聊天屏幕（默认）
    SCREEN_CAR_DATA,        ///< 车载数据页
    SCREEN_CAR_CAMERA,      ///< 车载摄像头页
    SCREEN_COUNT
} ScreenType;

/**
 * @class ScreenManager
 * @brief 屏幕管理器
 * @details 管理多个独立屏幕的创建和切换
 */
class ScreenManager {
public:
    /**
     * @brief 初始化屏幕管理器
     * @param display LCD 显示器指针
     * @return true=成功, false=失败
     */
    static bool Initialize(Display* display);

    /**
     * @brief 获取单例实例
     * @return ScreenManager 指针
     */
    static ScreenManager* GetInstance();

    /**
     * @brief 切换到指定屏幕
     * @param screen 目标屏幕
     * @param anim 是否使用动画
     */
    void SwitchToScreen(ScreenType screen, bool anim = true);

    /**
     * @brief 切换到下一个屏幕（循环切换）
     */
    void SwitchToNextScreen();

    /**
     * @brief 获取当前屏幕
     * @return 当前屏幕类型
     */
    ScreenType GetCurrentScreen();

    /**
     * @brief 获取车载数据页 UI 实例
     * @return CarUI 指针（可能为 nullptr）
     */
    CarUI* GetCarDataUI();

    /**
     * @brief 获取车载摄像头页 UI 实例
     * @return CarCameraUI 指针（可能为 nullptr）
     */
    CarCameraUI* GetCarCameraUI();

private:
    ScreenManager();
    ~ScreenManager();

    static ScreenManager* instance_;    ///< 单例实例

    lv_obj_t* chat_screen_;             ///< 聊天屏幕（原有屏幕）
    lv_obj_t* car_data_screen_;         ///< 车载数据页屏幕（独立创建）
    lv_obj_t* car_camera_screen_;       ///< 车载摄像头页屏幕（独立创建）
    CarUI* car_data_ui_;                ///< 数据页 UI 实例
    CarCameraUI* car_camera_ui_;        ///< 摄像头页 UI 实例
    Display* display_;                  ///< LCD 显示器
    ScreenType current_screen_;         ///< 当前屏幕
};
