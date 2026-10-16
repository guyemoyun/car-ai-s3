/**
 * @file car_ui_helper.h
 * @brief 车载监测界面辅助函数（基于 ScreenManager 架构）
 * @details 提供 CarMonitor 与 CarUI 之间的集成接口
 */

#pragma once

#include "car_ui.h"  // ← 修复：不需要 car_monitor/ 前缀

/**
 * @class CarUIHelper
 * @brief 车载界面辅助类
 * @details 配合 ScreenManager 管理车载监测界面
 */
class CarUIHelper {
public:
    /**
     * @brief 初始化车载界面（从 ScreenManager 获取实例并设置回调）
     * @return true=成功, false=失败
     */
    static bool Initialize();

    /**
     * @brief 启动界面更新任务
     * @param update_interval_ms 更新间隔（毫秒）
     * @return true=成功, false=失败
     */
    static bool StartUpdateTask(uint32_t update_interval_ms = 500);

    /**
     * @brief 停止界面更新任务
     */
    static void StopUpdateTask();

    /**
     * @brief 手动更新界面（从 CarMonitor 获取最新数据）
     */
    static void UpdateUI();

    /**
     * @brief 切换到车载监测屏幕
     */
    static void SwitchToCarMonitorScreen();

    /**
     * @brief 切换到聊天屏幕
     */
    static void SwitchToChatScreen();

    /**
     * @brief 在两个屏幕之间切换
     */
    static void ToggleScreen();

private:
    static TaskHandle_t update_task_; ///< 更新任务句柄

    /**
     * @brief 界面更新任务函数
     * @param arg 任务参数（更新间隔）
     */
    static void UpdateTaskFunction(void* arg);

    /**
     * @brief 切换车辆状态按钮回调
     */
    static void OnToggleVehicle();

    /**
     * @brief 确认告警按钮回调
     */
    static void OnConfirmAlert();

    /**
     * @brief 测试告警按钮回调
     */
    static void OnTestAlert();
};
