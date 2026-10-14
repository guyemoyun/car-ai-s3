/**
 * @file app_ui_manager.h
 * @brief 应用程序 UI 管理器
 * @details 统一管理所有 UI 页面（聊天、车载监测、设置）和屏保
 */

#pragma once

#include "page_manager.h"
#include "car_ui.h"
#include "display/display.h"
#include <lvgl.h>

/**
 * @class AppUIManager
 * @brief 应用 UI 管理器
 * @details 单例模式，统一管理页面切换、屏保控制、按钮反馈
 */
class AppUIManager {
public:
    /**
     * @brief 初始化 UI 管理器
     * @param display LCD 显示器指针
     * @return true=成功, false=失败
     */
    static bool Initialize(Display* display);

    /**
     * @brief 获取单例实例
     * @return AppUIManager 指针
     */
    static AppUIManager* GetInstance();

    /**
     * @brief 切换到指定页面
     * @param page 目标页面
     * @param disable_screensaver 是否禁用屏保（true=切换到该页面时停止屏保）
     */
    void SwitchToPage(PageType page, bool disable_screensaver = true);

    /**
     * @brief 获取当前页面
     * @return 当前页面类型
     */
    PageType GetCurrentPage();

    /**
     * @brief 获取指定页面的容器（用于放置子界面）
     * @param page 页面类型
     * @return LVGL 对象指针
     */
    lv_obj_t* GetPageContainer(PageType page);

    /**
     * @brief 启用/禁用屏保
     * @param enable true=允许屏保, false=禁止屏保
     */
    void SetScreensaverEnabled(bool enable);

    /**
     * @brief 获取车载 UI 实例
     * @return CarUI 指针（可能为 nullptr）
     */
    CarUI* GetCarUI();

    /**
     * @brief 显示按钮按下反馈（震动效果）
     * @param button 按钮对象
     */
    static void ShowButtonFeedback(lv_obj_t* button);

    /**
     * @brief 显示 Toast 提示消息
     * @param message 提示文本
     * @param duration_ms 显示时长（毫秒）
     */
    void ShowToast(const char* message, uint32_t duration_ms = 2000);

private:
    AppUIManager();
    ~AppUIManager();

    static AppUIManager* instance_;     ///< 单例实例
    PageManager* page_manager_;         ///< 页面管理器
    CarUI* car_ui_;                     ///< 车载监测界面
    Display* display_;                  ///< LCD 显示器
    lv_obj_t* toast_label_;             ///< Toast 提示标签
    bool screensaver_enabled_;          ///< 屏保启用标志

    /**
     * @brief Toast 消失定时器回调
     * @param timer 定时器对象
     */
    static void ToastTimerCallback(lv_timer_t* timer);
};
