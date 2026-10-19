/**
 * @file car_person_detect.h
 * @brief 本地人员检测（M4）：摄像头帧 → esp-dl 推理 → 上报车载监测
 * @details 单一职责：唯一持有 PedestrianDetect 与识别帧缓冲。
 *          检测结果经 CarMonitorHelper::OnPersonDetectionResult() 进入既有告警规则。
 *          本文件是全工程**唯一**调用 PedestrianDetect::run() 的地方（run 非线程安全）。
 */

#pragma once

#include <cstdint>

#include "car_camera_ui.h"  // CarDetectionBox（检测框 POD，UI 叠加层复用同一结构）

class CarPersonDetect {
public:
    /**
     * @brief 启动人员检测任务
     * @param interval_ms      正常监测的检测周期（毫秒）。**必须 < 2000**，
     *                         否则 car_monitor 的 person_data 失效、遗留告警判定直接失效
     * @param chat_interval_ms 小智会话期间的检测周期（毫秒），降频让出 PSRAM 带宽
     * @return true=已启动；false=已在运行或任务创建失败
     */
    static bool Start(uint32_t interval_ms = 500, uint32_t chat_interval_ms = 1000);

    /**
     * @brief 停止检测任务（诊断用；模型与帧缓冲不释放）
     */
    static void Stop();

    /**
     * @brief 是否正在运行
     */
    static bool IsRunning();

    /**
     * @brief 注册检测框出口（用于摄像头页叠加显示检测框，M4）
     * @param sink 回调：count=检测到的人数；boxes=检测框数组（源 320x240 坐标）；
     *             n=数组长度（最多 4 个，超出部分丢弃）
     * @note 回调运行在**人员检测任务上下文**（CPU0/优先级 1），不在 LVGL 线程：
     *       sink 内若操作 LVGL 必须自行取 lvgl_port_lock，且必须短小、不可阻塞；
     *       传 nullptr 取消注册。未注册时只剩 OnPersonDetectionResult 上报（告警不受影响）。
     * @warning 回调收到的 boxes 是**当次检测的栈上副本**，不可跨帧持有。
     */
    static void SetResultSink(void (*sink)(int count, const CarDetectionBox* boxes, int n));

private:
    static void TaskFunc(void* arg);
};
