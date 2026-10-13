#pragma once

#include "car_snapshot.h"

/**
 * @file virtual_sensors.h
 * @brief 虚拟传感器驱动接口
 * @details 用于演示模式下生成模拟温湿度和光照数据
 */

/**
 * @class VirtualSensors
 * @brief 虚拟传感器管理类
 */
class VirtualSensors {
public:
    /**
     * @brief 初始化虚拟传感器
     */
    static void Init();

    /**
     * @brief 读取虚拟温度数据
     * @return 温度值 (°C)
     */
    static float ReadTemperature();

    /**
     * @brief 读取虚拟湿度数据
     * @return 湿度值 (%)
     */
    static float ReadHumidity();

    /**
     * @brief 读取虚拟光照数据
     * @return 光照百分比 (0-100%)
     */
    static float ReadLight();

    /**
     * @brief 获取虚拟传感器生成器实例
     * @return 指向生成器的指针
     */
    static VirtualSensorGenerator* GetGenerator();

private:
    static VirtualSensorGenerator generator_;
};
