/**
 * @file qmi8658.cc
 * @brief QMI8658A 姿态传感器驱动与运动检测任务实现（见 qmi8658.h 的移植说明）
 */

#include "qmi8658.h"

#include "board.h"               // Board::GetInstance().GetI2cBus()
#include "car_monitor_helper.h"  // CarMonitorHelper::UpdateMotionState()

#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>

#define TAG "IMU"

namespace {

// ---- 寄存器地址（对应例程枚举的数值，见 01-base/07-i2c_attitude）----
constexpr uint8_t kRegWhoAmI  = 0x00;  // 期望 0x05
constexpr uint8_t kRegCtrl1   = 0x02;
constexpr uint8_t kRegCtrl2   = 0x03;
constexpr uint8_t kRegCtrl3   = 0x04;
constexpr uint8_t kRegCtrl7   = 0x08;
constexpr uint8_t kRegReset   = 0x60;
constexpr uint8_t kRegStatus0 = 0x2E;  // bit0=ACC 就绪，bit1=GYR 就绪
constexpr uint8_t kRegAccXLow = 0x35;  // 之后连续 12 字：ACC xyz + GYR xyz（小端）

constexpr uint32_t kTimeoutMs = 50;    // 单次 I2C 事务超时

// ---- 量纲换算（由 CTRL2/CTRL3 的档位决定，见 qmi8658.h）----
constexpr int kAccLsbPerG     = 8192;  // ±4g 16 位 → 8192 LSB/g
constexpr int kGyroLsbPerDps  = 64;    // ±512dps 16 位 → 64 LSB/dps

// ---- 运动判决参数（现场标定用日志里的 acc_ptp / gyro_mean 回调这两个阈值）----
// 滑窗：16 样本 × 40ms ≈ 0.64 s
constexpr int kWindowSamples     = 16;
// 进入"运动"：窗内加速度峰峰值 ≥60 mg，或陀螺平均幅值 ≥3 dps
constexpr int kAccPtpMgEnter     = 60;
constexpr int kGyroMeanDpsEnter  = 3;
// 退出"运动"：窗内加速度峰峰值 ≤30 mg 且陀螺平均幅值 ≤1 dps
constexpr int kAccPtpMgExit      = 30;
constexpr int kGyroMeanDpsExit   = 1;
constexpr int kEnterWindows      = 2;   // 连续 2 窗（≈1.3 s）满足才判"运动"
constexpr int kExitWindows       = 3;   // 连续 3 窗（≈1.9 s）满足才判"静止"
// 连续这么多次"没拿到有效数据"（I2C 失败，或数据长期不就绪）即标记传感器故障：
// 50 × 40ms ≈ 2 s
constexpr int kBadStreakFault    = 50;

// 标定期开关：true = 每个滑窗打一行 acc_ptp / gyro_mean / moving，
// 供现场标定上面 4 个阈值；稳定后可置 false 减少日志。
constexpr bool kLogEveryWindow = true;

TaskHandle_t s_task = nullptr;

}  // namespace

Qmi8658::~Qmi8658() {
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);   // 只移除本器件，不影响总线上其他器件
        dev_ = nullptr;
    }
}

bool Qmi8658::WriteReg(uint8_t reg, uint8_t value) const {
    if (dev_ == nullptr) {
        return false;
    }
    const uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(dev_, buf, sizeof(buf), kTimeoutMs) == ESP_OK;
}

bool Qmi8658::ReadRegs(uint8_t reg, uint8_t* buffer, size_t length) const {
    if (dev_ == nullptr || buffer == nullptr) {
        return false;
    }
    return i2c_master_transmit_receive(dev_, &reg, 1, buffer, length, kTimeoutMs) == ESP_OK;
}

bool Qmi8658::Init() {
    if (i2c_bus_ == nullptr) {
        ESP_LOGE(TAG, "没有共享 I2C 总线句柄");
        return false;
    }

    // 1) 按地址加设备句柄（不做探测，探测交给下面的 WHO_AM_I 读）
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = kAddress;
    cfg.scl_speed_hz = kSclHz;

    esp_err_t err = i2c_master_bus_add_device(i2c_bus_, &cfg, &dev_);
    if (err != ESP_OK || dev_ == nullptr) {
        ESP_LOGE(TAG, "I2C 加设备失败（%s），地址 0x%02X", esp_err_to_name(err), kAddress);
        dev_ = nullptr;
        return false;
    }

    // 2) 探测：单次读 WHO_AM_I，读不到就退出（不像原例程那样死循环等芯片）
    uint8_t id = 0;
    if (!ReadRegs(kRegWhoAmI, &id, 1)) {
        ESP_LOGW(TAG, "0x%02X 无应答（芯片未焊/接线异常/被占用）", kAddress);
        return false;
    }
    if (id != kWhoAmI) {
        ESP_LOGW(TAG, "WHO_AM_I=0x%02X，期望 0x%02X，不是 QMI8658A", id, kWhoAmI);
        return false;
    }

    // 3) 复位 + 配置（与例程完全一致的序列与取值）
    if (!WriteReg(kRegReset, 0xB0)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));  // 复位后等待
    if (!WriteReg(kRegCtrl1, 0x40)) {  // 寄存器地址自增（满足 12 字节连续读）
        return false;
    }
    if (!WriteReg(kRegCtrl2, 0x95)) {  // 加速度：±4g @ 250Hz
        return false;
    }
    if (!WriteReg(kRegCtrl3, 0xD5)) {  // 陀螺仪：±512dps @ 250Hz
        return false;
    }
    if (!WriteReg(kRegCtrl7, 0x03)) {  // 使能加速度 + 陀螺仪
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));  // 等待首帧数据

    ESP_LOGI(TAG, "QMI8658A 就绪（0x%02X，WHO_AM_I=0x05）：ACC ±4g@250Hz / GYR ±512dps@250Hz",
             kAddress);
    return true;
}

Qmi8658::Sample Qmi8658::ReadSample(int16_t* acc, int16_t* gyro) const {
    if (acc == nullptr || gyro == nullptr) {
        return Sample::kNotReady;
    }

    uint8_t status = 0;
    if (!ReadRegs(kRegStatus0, &status, 1)) {
        return Sample::kError;
    }
    if ((status & 0x03) != 0x03) {  // 加速度与陀螺仪都就绪才取
        return Sample::kNotReady;
    }

    uint8_t buf[12] = {};
    if (!ReadRegs(kRegAccXLow, buf, sizeof(buf))) {
        return Sample::kError;
    }
    for (int i = 0; i < 3; i++) {
        acc[i]  = (int16_t)(((uint16_t)buf[i * 2 + 1] << 8) | buf[i * 2]);         // 小端
        gyro[i] = (int16_t)(((uint16_t)buf[6 + i * 2 + 1] << 8) | buf[6 + i * 2]);
    }
    return Sample::kOk;
}

bool Qmi8658::StartMotionTask(uint32_t interval_ms) {
    if (s_task != nullptr) {
        ESP_LOGW(TAG, "运动检测任务已在运行");
        return false;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(TaskFunc, "imu_motion", 2560,
                                             (void*)(uintptr_t)interval_ms,
                                             /*优先级*/ 2, &s_task, /*CPU0*/ 0);
    if (ret != pdPASS) {
        s_task = nullptr;
        ESP_LOGE(TAG, "运动检测任务创建失败");
        return false;
    }
    ESP_LOGI(TAG, "运动检测任务已启动（采样周期 %u ms）", (unsigned)interval_ms);
    return true;
}

void Qmi8658::StopMotionTask() {
    if (s_task != nullptr) {
        vTaskDelete(s_task);
        s_task = nullptr;
        ESP_LOGW(TAG, "运动检测任务已停止");
    }
}

bool Qmi8658::IsRunning() {
    return s_task != nullptr;
}

void Qmi8658::TaskFunc(void* arg) {
    const uint32_t interval_ms = (uint32_t)(uintptr_t)arg;

    i2c_master_bus_handle_t bus = Board::GetInstance().GetI2cBus();
    if (bus == nullptr) {
        ESP_LOGE(TAG, "本板没有提供共享 I2C 总线，运动检测不启动");
        CarMonitorHelper::UpdateMotionState(false, /*imu_fault=*/true);
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    Qmi8658* imu = new Qmi8658(bus);
    if (!imu->Init()) {
        ESP_LOGW(TAG, "姿态传感器不可用 —— 关闭运动检测；预览/音频/告警均不受影响");
        CarMonitorHelper::UpdateMotionState(false, /*imu_fault=*/true);
        delete imu;   // 析构会移除刚加上的设备句柄
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // 初始按"静止"上报（上一行已确认传感器可用）
    CarMonitorHelper::UpdateMotionState(false, /*imu_fault=*/false);

    TickType_t last_wake = xTaskGetTickCount();

    bool moving = false;
    int enter_cnt = 0;      // 连续满足"进入"条件的窗数
    int exit_cnt = 0;       // 连续满足"退出"条件的窗数
    int bad_streak = 0;     // 连续"没拿到有效数据"次数（读失败或数据不就绪）
    bool fault_reported = false;

    // 当前滑窗累计
    int samples = 0;
    int acc_min = 0;
    int acc_max = 0;
    float gyro_sum = 0.0f;

    while (true) {
        int16_t acc[3] = {0, 0, 0};
        int16_t gyro[3] = {0, 0, 0};
        const Sample r = imu->ReadSample(acc, gyro);

        if (r != Sample::kOk) {
            // kError（I2C 失败）与 kNotReady（数据未就绪）都不计入滑窗，
            // 但连续太久仍标记故障，避免"传感器坏了却永远显示静止"。
            if (++bad_streak >= kBadStreakFault && !fault_reported) {
                fault_reported = true;
                CarMonitorHelper::UpdateMotionState(moving, /*imu_fault=*/true);
                ESP_LOGW(TAG, "连续 %d 次未取到有效数据（%s），标记姿态传感器故障（仍继续重试）",
                         bad_streak, (r == Sample::kError) ? "I2C 读失败" : "数据未就绪");
            }
        } else {
            if (bad_streak > 0) {
                bad_streak = 0;
            }
            if (fault_reported) {
                fault_reported = false;
                ESP_LOGI(TAG, "姿态传感器恢复正常");
            }

            const float ax = (float)acc[0];
            const float ay = (float)acc[1];
            const float az = (float)acc[2];
            const float gx = (float)gyro[0];
            const float gy = (float)gyro[1];
            const float gz = (float)gyro[2];

            const int acc_mag = (int)sqrtf(ax * ax + ay * ay + az * az);  // 合成加速度（LSB）
            if (samples == 0) {
                acc_min = acc_mag;
                acc_max = acc_mag;
            } else {
                if (acc_mag < acc_min) acc_min = acc_mag;
                if (acc_mag > acc_max) acc_max = acc_mag;
            }
            gyro_sum += sqrtf(gx * gx + gy * gy + gz * gz);
            samples++;

            if (samples >= kWindowSamples) {
                // 换算到物理量：加速度用峰峰值(mg)，陀螺用平均幅值(dps)
                const int acc_ptp_mg = (acc_max - acc_min) * 1000 / kAccLsbPerG;
                const int gyro_mean_dps =
                    (int)(gyro_sum / (float)kWindowSamples / (float)kGyroLsbPerDps + 0.5f);

                const bool enter_ok =
                    (acc_ptp_mg >= kAccPtpMgEnter) || (gyro_mean_dps >= kGyroMeanDpsEnter);
                const bool exit_ok =
                    (acc_ptp_mg <= kAccPtpMgExit) && (gyro_mean_dps <= kGyroMeanDpsExit);

                if (!moving) {
                    if (enter_ok) {
                        if (++enter_cnt >= kEnterWindows) {
                            moving = true;
                            enter_cnt = 0;
                            ESP_LOGI(TAG, "运动状态：运动（车辆在动/有振动）");
                        }
                    } else {
                        enter_cnt = 0;
                    }
                } else {
                    if (exit_ok) {
                        if (++exit_cnt >= kExitWindows) {
                            moving = false;
                            exit_cnt = 0;
                            ESP_LOGI(TAG, "运动状态：静止");
                        }
                    } else {
                        exit_cnt = 0;
                    }
                }

                if (kLogEveryWindow) {
                    ESP_LOGI(TAG, "IMU: acc_ptp=%d mg gyro_mean=%d dps moving=%d",
                             acc_ptp_mg, gyro_mean_dps, (int)moving);
                }

                CarMonitorHelper::UpdateMotionState(moving, fault_reported);

                // 复位滑窗
                samples = 0;
                gyro_sum = 0.0f;
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(interval_ms));
    }
}
