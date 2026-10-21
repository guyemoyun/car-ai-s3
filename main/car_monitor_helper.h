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
     * @brief 提交"切换车辆启停状态"命令（行驶中 ⇄ 停车）
     * @details 只往 car_monitor 的命令队列投递 CMD_TOGGLE_VEHICLE_STATE，由 car_monitor 任务
     *          异步生效（想确认结果请读快照的 vehicle_state）。
     * @note 调用方：MCP 工具 `self.car.toggle_vehicle_state`（语音切换，当前唯一可用入口）；
     *       摄像头页那颗"切换状态"屏幕按钮的接线（CarUIHelper::OnToggleVehicle）也走这里，
     *       但该按钮控件已移除（触摸不稳时删的），接口仍保留。
     * @warning **GPIO11 不是车辆状态按键**：本板 GPIO11 已被 SW3 占用，用途是切换屏幕
     *          （见 grw_esp32s3_board.cc 的 SW3 注册）。旧注释"GPIO11 按键切换车辆状态"已作废。
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
     * @brief 更新网络链路状态（由云上报模块每秒调用）
     * @param wifi_connected  Wi-Fi 是否已连上
     * @param cloud_connected 巴法云 MQTT 是否已连上
     */
    static void UpdateNetworkState(bool wifi_connected, bool cloud_connected);

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
     * @details 注册五个工具：环境查询、状态查询、告警查询、确认告警、切换车辆状态。
     *          全部只读快照或提交命令，与 CarMonitor 的线程安全接口对齐。
     * @note 必须在 MCP 会话建立之前调用（Application 启动流程中
     *       `McpServer::AddCommonTools()` 之后）；重复调用不会重复注册。
     */
    static void RegisterMcpTools();
};