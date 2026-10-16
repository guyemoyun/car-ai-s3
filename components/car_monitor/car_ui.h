/**
 * @file car_ui.h
 * @brief 车载数据页 LVGL 界面
 * @details 显示车辆状态、环境数据、人员检测和告警信息
 *
 * 布局（480x320 横屏，两栏网格，整页不滚动）：
 *   车辆状态        通栏置顶
 *   环境数据 | 人员检测   左右两栏
 *   告警            通栏置底
 *
 * 交互按钮已全部移出到摄像头页（CarCameraUI），本页只负责显示。
 */

#pragma once

#include "car_snapshot.h"
#include <lvgl.h>
#include <cstdint>

/**
 * @class CarUI
 * @brief 车载数据页界面类
 * @details 只显示数据、不承载任何按钮；内容固定，容器禁用滚动
 */
class CarUI {
public:
    /**
     * @brief 构造函数
     * @param parent LVGL 父对象（nullptr 使用默认屏幕）
     */
    explicit CarUI(lv_obj_t* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~CarUI();

    /**
     * @brief 更新界面显示
     * @param snapshot 状态快照数据
     */
    void Update(const CarSnapshot& snapshot);

    /**
     * @brief 显示/隐藏界面
     * @param visible true=显示, false=隐藏
     */
    void SetVisible(bool visible);

    /**
     * @brief 获取根对象
     * @return LVGL 对象指针
     */
    lv_obj_t* GetRoot() { return container_; }

private:
    lv_obj_t* container_;           ///< 主容器

    // 状态显示标签
    lv_obj_t* label_vehicle_state_; ///< 车辆状态标签（顶部通栏）
    lv_obj_t* label_temperature_;   ///< 温度标签（左栏）
    lv_obj_t* label_humidity_;      ///< 湿度标签（左栏）
    lv_obj_t* label_light_;         ///< 光照标签（左栏）
    lv_obj_t* label_person_count_;  ///< 人员数量标签（右栏）
    lv_obj_t* label_confidence_;    ///< 置信度标签（右栏）
    lv_obj_t* label_alert_;         ///< 告警标签（底部通栏）

    /**
     * @brief 创建界面布局（顶部通栏 + 中部两栏 + 底部通栏）
     */
    void CreateLayout();

    /**
     * @brief 创建左栏：环境数据
     * @param parent 父对象（中部横向容器）
     */
    void CreateEnvPanel(lv_obj_t* parent);

    /**
     * @brief 创建右栏：人员检测
     * @param parent 父对象（中部横向容器）
     */
    void CreatePersonPanel(lv_obj_t* parent);

    /**
     * @brief 格式化告警类型为字符串
     * @param alert 告警类型
     * @return 告警描述字符串
     */
    static const char* AlertToString(AlertType alert);

    /**
     * @brief 格式化车辆状态为字符串
     * @param state 车辆状态
     * @return 状态描述字符串
     */
    static const char* VehicleStateToString(CarVehicleState state);
};
