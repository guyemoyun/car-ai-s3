#pragma once

#ifndef __CAR_SNAPSHOT_H__
#define __CAR_SNAPSHOT_H__

#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>

/**
 * @file car_snapshot.h
 * @brief 车载监测状态快照数据结构
 * @details 定义车辆、人员、环境、告警等所有状态的一致性快照
 */

// ============================================================================
// 车辆状态枚举
// ============================================================================

/**
 * @enum CarVehicleState
 * @brief 车辆运行状态
 */
typedef enum {
    CAR_STATE_RUNNING = 0,  /**< 行驶状态 */
    CAR_STATE_PARKED = 1,   /**< 停车状态 */
    CAR_STATE_UNKNOWN = 2   /**< 未知状态 */
} CarVehicleState;

// ============================================================================
// 人员状态枚举
// ============================================================================

/**
 * @enum CarPersonState
 * @brief 车内人员状态
 */
typedef enum {
    PERSON_STATE_UNKNOWN = 0,   /**< 检测故障或结果过期 */
    PERSON_STATE_EMPTY = 1,     /**< 确认无人 */
    PERSON_STATE_PRESENT = 2    /**< 确认有人 */
} CarPersonState;

// ============================================================================
// 告警事件类型
// ============================================================================

/**
 * @enum AlertType
 * @brief 告警事件类型
 */
typedef enum {
    ALERT_NONE = 0,
    ALERT_PERSON_LEFT = 1,      /**< 人员遗留告警 */
    ALERT_TEMP_HIGH = 2,        /**< 温度过高 */
    ALERT_HUMIDITY_HIGH = 3,    /**< 湿度过高 */
    ALERT_LIGHT_ABNORMAL = 4    /**< 光照异常 */
} AlertType;

/**
 * @struct AlertEvent
 * @brief 告警事件记录
 */
typedef struct {
    uint32_t event_id;              /**< 事件编号（递增） */
    AlertType type;                 /**< 告警类型 */
    uint32_t timestamp;             /**< 触发时间戳（ms） */
    bool is_active;                 /**< 是否激活（true=活跃，false=已确认/已解除） */
    uint32_t confirmed_timestamp;   /**< 用户确认时间 */
} AlertEvent;

// ============================================================================
// 环境数据结构
// ============================================================================

/**
 * @struct EnvironmentData
 * @brief 环境监测数据
 */
typedef struct {
    float temperature;              /**< 温度 (°C) */
    bool temperature_valid;         /**< 温度数据是否有效 */
    uint32_t temperature_age_ms;    /**< 温度数据年龄 (ms) */

    float humidity;                 /**< 湿度 (%) */
    bool humidity_valid;            /**< 湿度数据是否有效 */
    uint32_t humidity_age_ms;       /**< 湿度数据年龄 (ms) */

    float light_percent;            /**< 光照百分比 (0-100%) */
    bool light_valid;               /**< 光照数据是否有效 */
    uint32_t light_age_ms;          /**< 光照数据年龄 (ms) */
} EnvironmentData;

// ============================================================================
// 人员检测数据
// ============================================================================

/**
 * @struct PersonDetectionData
 * @brief 人员检测结果
 */
typedef struct {
    CarPersonState state;           /**< 当前人员状态 */
    int person_count;               /**< 检测到的人数 */
    float confidence;               /**< 检测置信度 (0.0-1.0) */
    bool valid;                     /**< 本次检测是否有效 */
    uint32_t detection_age_ms;      /**< 检测结果年龄 (ms) */
    uint32_t last_detection_time;   /**< 最后检测时间戳 */
} PersonDetectionData;

// ============================================================================
// 完整快照结构
// ============================================================================

/**
 * @struct CarSnapshot
 * @brief 车载系统状态快照
 * @details 通过GetSnapshot()获取一致的状态副本，所有数据在同一时刻获取
 */
typedef struct {
    // 基础信息
    bool demo_mode;                 /**< 是否处于演示模式（使用虚拟数据） */
    uint32_t snapshot_id;           /**< 快照序号 */
    uint32_t timestamp;             /**< 快照时间戳 (ms) */

    // 车辆状态
    CarVehicleState vehicle_state;  /**< 车辆运行/停车状态 */
    uint32_t vehicle_state_change_time; /**< 状态改变时间 */

    // 环境数据
    EnvironmentData env_data;       /**< 环境监测数据 */

    // 人员检测
    PersonDetectionData person_data; /**< 人员检测结果 */

    // 运动状态（IMU辅助，后续扩展）
    bool is_moving;                 /**< 是否在运动（基于IMU） */
    uint32_t motion_age_ms;         /**< 运动状态年龄 */

    // 告警状态
    AlertType active_alert;         /**< 当前活跃告警类型 */
    uint32_t active_alert_timestamp; /**< 活跃告警时间 */
    bool alert_confirmed;           /**< 告警是否已被用户确认 */
    uint32_t alert_confirm_time;    /**< 确认时间 */

    // 故障标志
    bool temp_sensor_fault;         /**< 温度传感器故障 */
    bool humidity_sensor_fault;     /**< 湿度传感器故障 */
    bool camera_fault;              /**< 摄像头故障 */
    bool audio_fault;               /**< 音频系统故障 */
    bool i2c_fault;                 /**< I2C总线故障 */
    bool imu_fault;                 /**< 姿态传感器(QMI8658A)不可用：芯片缺失或持续读失败 */

    // 网络状态
    bool wifi_connected;            /**< WiFi连接状态 */
    bool cloud_connected;           /**< 云端连接状态 */

    // 用户交互状态
    bool in_ai_chat_mode;           /**< 是否在小智AI对话模式 */

} CarSnapshot;

// ============================================================================
// 虚拟数据生成器接口
// ============================================================================

/**
 * @brief 虚拟传感器数据生成器
 * @details 模拟真实传感器数据，用于演示模式
 */
typedef struct {
    float temp_base;                /**< 基础温度值 */
    float humidity_base;            /**< 基础湿度值 */
    float light_base;               /**< 基础光照值 */
    uint32_t last_update_time;      /**< 最后更新时间 */
} VirtualSensorGenerator;

#endif // __CAR_SNAPSHOT_H__
