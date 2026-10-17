/**
 * @file car_camera_ui.h
 * @brief 车载摄像头页 LVGL 界面
 * @details 4:3 预览占位区 + 三颗操作按钮，整页不滚动
 *
 * 三颗按钮（切换状态 / 确认告警 / 测试告警）按设计全部放在本页，
 * 数据页(CarUI)只显示数据，不再承载交互。
 */

#pragma once

#include <lvgl.h>
#include <cstdint>

/**
 * @class CarCameraUI
 * @brief 车载摄像头页界面类
 * @details 预览区按摄像头真实画幅 4:3 定尺寸；容器禁用滚动
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
     * @brief 获取根对象
     * @return LVGL 对象指针
     */
    lv_obj_t* GetRoot() { return container_; }

private:
    lv_obj_t* container_;           ///< 主容器
    lv_obj_t* preview_area_;        ///< 摄像头预览区（4:3 画幅）
    lv_obj_t* preview_img_;         ///< 实时画面（lv_image，320x240 1:1）
    lv_obj_t* preview_placeholder_; ///< "等待画面"占位文字，首帧到达后隐藏

    // 交互按钮
    lv_obj_t* btn_toggle_vehicle_;  ///< 切换车辆状态按钮
    lv_obj_t* btn_confirm_alert_;   ///< 确认告警按钮
    lv_obj_t* btn_test_alert_;      ///< 测试告警按钮

    // 回调函数指针
    void (*toggle_vehicle_callback_)(void);
    void (*confirm_alert_callback_)(void);
    void (*test_alert_callback_)(void);

    /**
     * @brief 创建界面布局（预览区 + 按钮行）
     */
    void CreateLayout();

    /**
     * @brief 创建按钮行（三颗按钮横排）
     * @param parent 父对象
     */
    void CreateButtonArea(lv_obj_t* parent);

    /**
     * @brief LVGL 按钮事件回调
     * @param e 事件对象
     */
    static void ButtonEventCallback(lv_event_t* e);
};
