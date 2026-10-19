/**
 * @file car_monitor_helper.h
 * @brief 车载监测系统集成辅助函数
 * @details 提供车载监测系统与应用程序的集成接口
 */

#pragma once

#include "car_monitor.h"
#include <esp_log.h>

/**
 * @brief 车载监测辅助类
 * @details 封装车载监测系统的常用操作，提供便捷接口
 */
class CarMonitorHelper {
public:
    /**
     * @brief 查询并打印车载监测状态（用于调试）
     * @details 读取当前状态快照并通过日志输出
     */
    static void PrintStatus();

    /**
     * @brief 处理用户按键（GPIO11）- 切换车辆启停状态
     * @details 当用户按下GPIO11按键时调用此函数
     * @note 行驶中→停车，停车→行驶中
     */
    static void HandleUserButtonPress();

    /**
     * @brief 处理触摸屏确认告警按钮
     * @details 当用户在屏幕上点击"确认告警"按钮时调用
     * @note 告警将被静音30秒，新的告警仍会正常提示
     */
    static void HandleConfirmAlertButton();

    /**
     * @brief 触发测试告警（用于开发调试）
     * @details 手动触发一个人员遗留告警，用于测试告警流程
     */
    static void HandleTestAlertButton();

    /**
     * @brief 更新人员检测结果（由人员检测模块调用）
     * @param person_count 检测到的人数（0=无人，1或更多=有人）
     * @param confidence 检测置信度（0.0-1.0）
     * @details 人员检测模块每次完成检测后调用此函数更新结果
     * @note 此函数会被人员检测模块在检测完成回调中调用
     */
    static void OnPersonDetectionResult(int person_count, float confidence);

    /**
     * @brief 更新车辆运动状态（由姿态传感器模块调用）
     * @param is_moving 当前是否处于运动/振动状态
     * @param imu_fault true=姿态传感器不可用，此时 is_moving 无意义
     * @note 只写入快照辅助字段（is_moving / motion_age_ms / imu_fault），不参与告警判定
     */
    static void UpdateMotionState(bool is_moving, bool imu_fault);

    /**
     * @brief 检查是否有活跃告警
     * @return 当前活跃的告警类型（ALERT_NONE表示无告警）
     */
    static AlertType CheckActiveAlert();

    /**
     * @brief 获取当前车辆状态
     * @return 车辆状态（RUNNING=行驶中，PARKED=已停车）
     */
    static CarVehicleState GetVehicleState();

    /**
     * @brief 获取当前环境数据
     * @param temp 输出温度值（°C）
     * @param humidity 输出湿度值（%）
     * @param light 输出光照值（%）
     * @return true=数据有效，false=数据无效或未初始化
     */
    static bool GetEnvironmentData(float* temp, float* humidity, float* light);

    /**
     * @brief 获取人员检测状态
     * @param person_count 输出检测到的人数
     * @param confidence 输出检测置信度
     * @return true=数据有效，false=数据无效或未初始化
     */
    static bool GetPersonDetectionData(int* person_count, float* confidence);

    /**
     * @brief 注册车载 MCP 工具（`self.car.*`）
     * @details 注册四个工具：环境查询、状态查询、告警查询、确认告警。
     *          全部只读快照或提交命令，与 CarMonitor 的线程安全接口对齐。
     * @note 必须在 MCP 会话建立之前调用（Application 启动流程中
     *       `McpServer::AddCommonTools()` 之后）；重复调用不会重复注册。
     */
    static void RegisterMcpTools();
};