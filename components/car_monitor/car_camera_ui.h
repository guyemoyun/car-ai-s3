/**
 * @file car_camera_ui.h
 * @brief 车载摄像头页 LVGL 界面
 * @details 4:3 预览区（320x240，1:1 显示），整页不滚动
 *
 * 注：原设计在本页横排放三颗操作按钮（切换状态 / 确认告警 / 测试告警）。
 * 因屏幕触摸尚不稳定，**按键控件已暂时移除**；三个 Set*Callback 接口与
 * main 侧的接线保留，便于日后恢复。
 */

#pragma once

#include <lvgl.h>
#include <cstdint>

/**
 * @struct CarDetectionBox
 * @brief 单个检测框（源画面像素坐标，左上原点，x2/y2 为右下）
 * @details 源画幅 320x240 与预览内区同尺寸 => 坐标为 1:1，可直接当 LVGL 摆位用。
 *          仅存坐标与整数化置信度（本工程 newlib 为 nano 格式，%f 不可靠）。
 */
struct CarDetectionBox {
    int16_t x1;          ///< 左上 x
    int16_t y1;          ///< 左上 y
    int16_t x2;          ///< 右下 x
    int16_t y2;          ///< 右下 y
    int16_t score_x100;  ///< 置信度 ×100
};

/**
 * @class CarCameraUI
 * @brief 车载摄像头页界面类
 * @details 预览区按摄像头画幅 320x240(4:3) 定尺寸；容器禁用滚动；当前无按键控件
 */
class CarCameraUI {
public:
    /**
     * @brief 构造函数
     * @param parent LVGL 父对象（nullptr 使用默认屏幕）
     */
    explicit CarCameraUI(lv_obj_t* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~CarCameraUI();

    /**
     * @brief 设置切换车辆状态按钮回调
     * @param callback 回调函数
     */
    void SetToggleVehicleCallback(void (*callback)(void));

    /**
     * @brief 设置确认告警按钮回调
     * @param callback 回调函数
     */
    void SetConfirmAlertCallback(void (*callback)(void));

    /**
     * @brief 设置测试告警按钮回调
     * @param callback 回调函数
     */
    void SetTestAlertCallback(void (*callback)(void));

    /**
     * @brief 显示/隐藏界面
     * @param visible true=显示, false=隐藏
     */
    void SetVisible(bool visible);

    /**
     * @brief 刷新预览画面（由预览任务在 LVGL 锁内调用）
     * @param dsc RGB565 图像描述符（数据须为 LVGL 小端，缓冲在 PSRAM 即可——
     *            LVGL 会先拷进内部 DMA 绘制缓冲再推屏，故无内部 RAM 压力）
     * @note 源尺寸与画幅内区同为 320x240，按 1:1 blit，不做软件缩放
     */
    void SetFrame(const lv_img_dsc_t* dsc);

    /**
     * @brief 刷新预览画面上的检测框叠加层（M4）
     * @param count 检测到的人数（0 = 隐藏所有框）
     * @param boxes 检测框数组（源 320x240 坐标，与预览内区 1:1，无需换算）
     * @param n     框数量（最多 kMaxDetections 个，超出部分忽略）
     * @note 必须在 LVGL 锁内调用（与 SetFrame 同）；本函数只摆位/显隐，不重绘整个画面
     */
    void SetDetections(int count, const CarDetectionBox* boxes, int n);

    /// 叠加层最多绘制的框数（模型 NMS 后通常 0~2 个，留余量）
    static constexpr int kMaxDetections = 4;

    /**
     * @brief 获取根对象
     * @return LVGL 对象指针
     */
    lv_obj_t* GetRoot() { return container_; }

private:
    lv_obj_t* container_;           ///< 主容器
    lv_obj_t* preview_area_;        ///< 摄像头预览区（4:3 画幅）
    lv_obj_t* preview_img_;         ///< 实时画面（lv_image，320x240 1:1）
    lv_obj_t* preview_placeholder_; ///< "等待画面"占位文字，首帧到达后隐藏

    lv_obj_t* overlay_;                                  ///< 检测框叠加层（透明，尺寸同预览内区）
    lv_obj_t* detect_boxes_[kMaxDetections];              ///< 叠加层内的空心框

    // 回调函数指针（三颗屏幕按键已暂时移除；保留接线，便于将来恢复）
    void (*toggle_vehicle_callback_)(void);
    void (*confirm_alert_callback_)(void);
    void (*test_alert_callback_)(void);

    /**
     * @brief 创建界面布局（当前只有预览区）
     */
    void CreateLayout();
};
