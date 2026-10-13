#pragma once

#include "car_snapshot.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstdint>

/**
 * @file car_monitor.h
 * @brief 车载监测协调器核心接口
 * @details
 * 管理车辆状态、人员检测、环境数据采集和告警逻辑
 * 通过命令队列接收外部控制，通过快照提供一致的状态查询
 */

// ============================================================================
// 命令类型定义
// ============================================================================

/**
 * @enum CarCommandType
 * @brief 发送给car_monitor的命令类型
 */
typedef enum {
    CMD_QUERY_TEMPERATURE = 1,      /**< 查询温度 */
    CMD_QUERY_HUMIDITY = 2,         /**< 查询湿度 */
    CMD_QUERY_LIGHT = 3,            /**< 查询光照 */
    CMD_QUERY_STATUS = 4,           /**< 查询完整状态 */
    CMD_TOGGLE_VEHICLE_STATE = 5,   /**< 切换车辆启停状态 */
    CMD_CONFIRM_ALERT = 6,          /**< 确认告警 */
    CMD_TRIGGER_TEST_ALERT = 7,     /**< 触发测试告警（调试用） */
} CarCommandType;

/**
 * @struct CarCommand
 * @brief 命令结构
 */
typedef struct {
    CarCommandType type;            /**< 命令类型 */
    uint32_t param;                 /**< 命令参数 */
} CarCommand;

// ============================================================================
// CarMonitor主类
// ============================================================================

/**
 * @class CarMonitor
 * @brief 车载监测系统协调器
 * @details
 * 单例模式实现，统一管理所有车载监测业务逻辑
 * - 环境数据采集与管理
 * - 人员检测与遗留告警
 * - 告警生命周期管理
 * - 状态机驱动
 */
class CarMonitor {
public:
    /**
     * @brief 初始化car_monitor系统
     * @param demo_mode true=使用虚拟数据，false=等待真实硬件接入
     * @return true=初始化成功，false=初始化失败
     */
    static bool Init(bool demo_mode = true);

    /**
     * @brief 获取单例实例
     * @return CarMonitor单例指针
     */
    static CarMonitor* GetInstance();

    /**
     * @brief 启动监测任务
     * @param stack_size 任务栈大小（字节）
     * @param priority 任务优先级
     * @return true=任务启动成功
     */
    bool StartMonitorTask(uint32_t stack_size = 4096, uint32_t priority = 5);

    /**
     * @brief 提交命令到监测系统
     * @param cmd 命令结构体
     * @return true=命令接受，false=队列满或其他错误
     * @note 这是异步接口，命令会在监测任务中处理
     */
    bool SubmitCommand(const CarCommand& cmd);

    /**
     * @brief 获取当前状态快照
     * @param out_snapshot 输出的快照结构体指针
     * @return true=获取成功，false=尚未初始化
     * @note 该函数不触发硬件访问，返回最新的快照副本
     */
    bool GetSnapshot(CarSnapshot* out_snapshot);

    /**
     * @brief 更新人员检测结果
     * @param person_count 检测到的人数
     * @param confidence 检测置信度 (0.0-1.0)
     * @note 外部人员检测模块调用此接口提交结果
     */
    void UpdatePersonDetection(int person_count, float confidence);

    /**
     * @brief 查询是否有活跃告警
     * @return 当前活跃的告警类型
     */
    AlertType GetActiveAlert();

    /**
     * @brief 清除活跃告警（用户确认后调用）
     */
    void ClearActiveAlert();

    /**
     * @brief 获取监测任务句柄
     * @return FreeRTOS任务句柄，如果任务未启动返回NULL
     */
    TaskHandle_t GetMonitorTaskHandle();

    /**
     * @brief 析构函数
     */
    ~CarMonitor();

private:
    /**
     * @brief 私有构造函数（单例）
     */
    CarMonitor();

    // ========== 状态管理 ==========

    /**
     * @brief 更新环境数据
     */
    void UpdateEnvironmentData();

    /**
     * @brief 更新告警状态机
     */
    void UpdateAlertStateMachine();

    /**
     * @brief 处理车辆状态切换
     */
    void HandleVehicleStateChange();

    /**
     * @brief 检查停车遗留条件
     * @return true=需要触发告警
     */
    bool CheckPersonLeftAlert();

    /**
     * @brief 检查温度异常
     * @return true=需要触发告警
     */
    bool CheckTemperatureAlert();

    /**
     * @brief 检查湿度异常
     * @return true=需要触发告警
     */
    bool CheckHumidityAlert();

    // ========== 命令处理 ==========

    /**
     * @brief 处理接收到的命令
     */
    void ProcessCommand(const CarCommand& cmd);

    // ========== 监测任务 ==========

    /**
     * @brief FreeRTOS监测任务函数
     */
    static void MonitorTaskFunc(void* arg);

    // ========== 成员变量 ==========

    static CarMonitor* instance_;           /**< 单例实例 */
    static SemaphoreHandle_t instance_mutex_; /**< 实例互斥锁 */

    CarSnapshot current_snapshot_;          /**< 当前状态快照 */
    SemaphoreHandle_t snapshot_mutex_;      /**< 快照互斥保护 */

    QueueHandle_t command_queue_;           /**< 命令队列 */
    TaskHandle_t monitor_task_handle_;      /**< 监测任务句柄 */

    bool demo_mode_;                        /**< 演示模式标志 */
    bool initialized_;                      /**< 初始化标志 */

    // 状态机相关
    uint32_t parking_start_time_;           /**< 停车开始时间 */
    uint32_t last_person_detected_time_;    /**< 最后检测到人的时间 */
    int person_detection_count_;            /**< 连续检测到人的次数 */
    int empty_detection_count_;             /**< 连续检测到空的次数 */

    // 告警相关
    uint32_t alert_start_time_;             /**< 告警开始时间 */
    uint32_t alert_confirm_time_;           /**< 告警确认时间 */
};
