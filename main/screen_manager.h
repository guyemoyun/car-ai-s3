/**
 * @file screen_manager.h
 * @brief 屏幕管理器 - 管理聊天屏幕和车载监测屏幕的切换
 */

#pragma once

#include "car_ui.h"  // ← 修复：不需要前缀
#include "display/display.h"
#include <lvgl.h>

/**
 * @enum ScreenType
 * @brief 屏幕类型
 */
typedef enum {
    SCREEN_CHAT = 0,        ///< 聊天屏幕（默认）
    SCREEN_CAR_MONITOR,     ///< 车载监测屏幕
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
     * @brief 获取车载 UI 实例
     * @return CarUI 指针（可能为 nullptr）
     */
    CarUI* GetCarUI();

private:
    ScreenManager();
    ~ScreenManager();

    static ScreenManager* instance_;    ///< 单例实例

    lv_obj_t* chat_screen_;             ///< 聊天屏幕（原有屏幕）
    lv_obj_t* car_monitor_screen_;      ///< 车载监测屏幕（独立创建）
    CarUI* car_ui_;                     ///< 车载 UI 实例
    Display* display_;                  ///< LCD 显示器
    ScreenType current_screen_;         ///< 当前屏幕
};
