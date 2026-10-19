/**
 * @file qmi8658.h
 * @brief QMI8658A 姿态传感器（板载 IMU）：车辆运动状态检测
 *
 * 移植来源：`01-base/07-i2c_attitude` 与 `01-base/11-audio_es8311` 的 BSP
 * （同一颗芯片、同一组寄存器与初始化序列：0x6A / WHO_AM_I=0x05 / 复位 0xB0 /
 *  CTRL1=0x40 / CTRL2=0x95(ACC ±4g@250Hz) / CTRL3=0xD5(GYR ±512dps@250Hz) /
 *  CTRL7=0x03 使能 / STATUS0=0x2E / 数据从 0x35 起 12 字节）。
 *
 * 与原例程的两点差异（必须）：
 *  1. 原例程用旧 `driver/i2c.h` 且自己建总线；本工程已有一条共享 I2C1
 *     （SDA=GPIO1 / SCL=GPIO2，与 PCA9557、ES8311/7210、FT63x6 触摸共线），
 *     故这里取 `Board::GetInstance().GetI2cBus()` 后按地址加设备，**不新建总线**。
 *  2. **不继承 I2cDevice**：该基类每个事务都用 ESP_ERROR_CHECK，芯片缺失时的
 *     NACK 会直接 abort 整机。本驱动所有事务自行判错返回 bool，失败只降级。
 *
 * 职责边界：只产出"运动/静止"这一辅助状态写入 CarSnapshot
 * （is_moving / motion_age_ms / imu_fault），**不参与告警判定**
 * （计划口径：IMU 不替代车辆启停输入）。
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/i2c_master.h>

class Qmi8658 {
public:
    /// 器件 7 位地址
    static constexpr uint8_t kAddress = 0x6A;
    /// WHO_AM_I 期望值
    static constexpr uint8_t kWhoAmI = 0x05;
    /// I2C 时钟（与同总线上其他器件一致）
    static constexpr uint32_t kSclHz = 400 * 1000;

    /**
     * @brief 构造函数（只记录总线，不做任何 I2C 动作）
     * @param i2c_bus 共享 I2C 总线句柄（来自 Board::GetInstance().GetI2cBus()）
     */
    explicit Qmi8658(i2c_master_bus_handle_t i2c_bus) : i2c_bus_(i2c_bus) {}

    /** @brief 析构：释放本器件句柄（不影响总线上其他器件） */
    ~Qmi8658();

    /**
     * @brief 加设备句柄 → 探测 → 配置
     * @return true=芯片在位且配置写入成功；false=芯片缺失或总线异常（调用方须降级，
     *         不重试死等、不终止整机）
     * @note 探测是单次读取：读不到 WHO_AM_I=0x05 立即返回 false，不像原例程那样死循环等芯片。
     */
    bool Init();

    /// 单次采样结果
    enum class Sample : uint8_t {
        kOk,       ///< 读到一帧有效数据
        kNotReady, ///< 数据未就绪（跳过本次，不计错误）
        kError,    ///< I2C 事务失败（NACK/超时，计入故障判定）
    };

    /**
     * @brief 读一帧加速度 + 陀螺仪原始值
     * @param acc 输出 3 轴加速度原始值（LSB，±4g 时为 8192 LSB/g）
     * @param gyro 输出 3 轴角速度原始值（LSB，±512dps 时为 64 LSB/dps）
     * @return 采样结果（见 Sample）
     */
    Sample ReadSample(int16_t* acc, int16_t* gyro) const;

    /**
     * @brief 启动运动检测任务（采样 → 滑窗判决 → 写入车载快照）
     * @param interval_ms 采样周期，默认 40ms（25Hz）
     * @return true=任务已创建；false=已在运行或创建失败
     * @note 任务钉 CPU0 / 优先级 2 / 栈 2560 字节：低于预览(3)与 car_monitor(5)，
     *       且不占 CPU1（音频/AFE/wakenet 所在核）。
     */
    static bool StartMotionTask(uint32_t interval_ms = 40);

    /// 停止运动检测任务（诊断用；器件句柄随任务一起释放）
    static void StopMotionTask();

    /// 任务是否在运行
    static bool IsRunning();

private:
    bool WriteReg(uint8_t reg, uint8_t value) const;
    bool ReadRegs(uint8_t reg, uint8_t* buffer, size_t length) const;

    static void TaskFunc(void* arg);

    i2c_master_bus_handle_t i2c_bus_;              ///< 共享总线（不拥有）
    i2c_master_dev_handle_t dev_ = nullptr;        ///< 本器件句柄（Init 成功后持有）
};
