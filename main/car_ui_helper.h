/**
 * @file car_ui_helper.h
 * @brief 车载监测界面辅助函数
 * @details 提供 CarMonitor 与 CarUI 之间的集成接口
 */

#pragma once

#include "car_monitor/car_ui.h"
#include <lvgl.h>

/**
 * @class CarUIHelper
 * @brief 车载界面辅助类
 * @details 管理 CarUI 实例，提供便捷的集成接口
 */
class CarUIHelper {
public:
    /**
     * @brief 初始化车载界面
     * @param parent LVGL 父对象（nullptr 使用默认屏幕）
     * @return true=成功, false=失败
     */
    static bool Initialize(lv_obj_t* parent = nullptr);

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
     * @brief 显示/隐藏界面
     * @param visible true=显示, false=隐藏
     */
    static void SetVisible(bool visible);

    /**
     * @brief 获取 CarUI 实例
     * @return CarUI 指针（可能为 nullptr）
     */
    static CarUI* GetInstance();

private:
    static CarUI* ui_instance_;       ///< UI 实例
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
