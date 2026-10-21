#include "i2c_device.h"

#include <esp_log.h>

#define TAG "I2cDevice"

namespace {

/**
 * I2C 事务失败后的恢复：复位总线（打时钟脉冲解救被从机拉死的 SDA/SCL）再重试一次。
 * 不做恢复的话，只要有一次事务超时，后续所有事务都会失败。
 */
template <typename Op>
esp_err_t RetryWithBusReset(i2c_master_bus_handle_t bus, Op&& op) {
    esp_err_t err = op();
    if (err == ESP_OK) {
        return err;
    }
    ESP_LOGW(TAG, "I2C transaction failed: %s, resetting bus and retrying", esp_err_to_name(err));
    if (bus != nullptr) {
        esp_err_t reset_err = i2c_master_bus_reset(bus);
        if (reset_err != ESP_OK) {
            ESP_LOGW(TAG, "i2c_master_bus_reset failed: %s", esp_err_to_name(reset_err));
        }
        err = op();
    }
    return err;
}

}  // namespace

I2cDevice::I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : i2c_bus_(i2c_bus) {
    i2c_device_config_t i2c_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400 * 1000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    // 这里不能 abort：某颗从机缺席（NACK）或总线被拉死不应导致整机重启。
    // 原来用 ESP_ERROR_CHECK，只要 PCA9557 首次写入失败就会 abort -> 软复位 ->
    // 从机不复位 -> 再次失败，形成"开机黑屏死循环"（实测日志：i2c.master timeout
    // -> ESP_ERR_INVALID_STATE -> abort）。
    esp_err_t err = i2c_master_bus_add_device(i2c_bus, &i2c_device_cfg, &i2c_device_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device 0x%02x: %s", addr, esp_err_to_name(err));
        i2c_device_ = nullptr;
    }
}

void I2cDevice::WriteReg(uint8_t reg, uint8_t value) {
    if (i2c_device_ == nullptr) {
        return;
    }
    uint8_t buffer[2] = {reg, value};
    esp_err_t err = RetryWithBusReset(i2c_bus_, [&]() {
        return i2c_master_transmit(i2c_device_, buffer, 2, 100);
    });
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WriteReg(0x%02x, 0x%02x) failed: %s (skip, do not abort)",
                 reg, value, esp_err_to_name(err));
    }
}

uint8_t I2cDevice::ReadReg(uint8_t reg) {
    if (i2c_device_ == nullptr) {
        return 0;
    }
    uint8_t buffer[1] = {0};
    esp_err_t err = RetryWithBusReset(i2c_bus_, [&]() {
        return i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, 1, 100);
    });
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ReadReg(0x%02x) failed: %s (skip, do not abort)", reg, esp_err_to_name(err));
        return 0;
    }
    return buffer[0];
}

void I2cDevice::ReadRegs(uint8_t reg, uint8_t* buffer, size_t length) {
    if (i2c_device_ == nullptr) {
        return;
    }
    esp_err_t err = RetryWithBusReset(i2c_bus_, [&]() {
        return i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, length, 100);
    });
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ReadRegs(0x%02x, %u) failed: %s (skip, do not abort)",
                 reg, (unsigned)length, esp_err_to_name(err));
    }
}
