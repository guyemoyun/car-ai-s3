#include "virtual_sensors.h"
#include <esp_log.h>
#include <cmath>
#include <cstdlib>

static const char *TAG = "virtual_sensors";

// 静态成员初始化
VirtualSensorGenerator VirtualSensors::generator_ = {
    .temp_base = 25.0f,
    .humidity_base = 50.0f,
    .light_base = 50.0f,
    .last_update_time = 0
};

/**
 * @brief 生成随机波动数据
 * @param base_value 基础值
 * @param amplitude 波动幅度
 * @return 带波动的数据
 */
static float AddNoise(float base_value, float amplitude) {
    // 生成 -1.0 到 1.0 之间的随机数
    float random_factor = (float)(rand() % 200 - 100) / 100.0f;
    float noise = random_factor * amplitude;
    return base_value + noise;
}

/**
 * @brief 限制数值在指定范围内
 * @param value 值
 * @param min_val 最小值
 * @param max_val 最大值
 * @return 限制后的值
 */
static float Clamp(float value, float min_val, float max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

void VirtualSensors::Init() {
    ESP_LOGI(TAG, "Virtual sensors initialized (demo mode)");
    ESP_LOGI(TAG, "  Base temperature: %.1f°C", generator_.temp_base);
    ESP_LOGI(TAG, "  Base humidity: %.1f%%", generator_.humidity_base);
    ESP_LOGI(TAG, "  Base light: %.1f%%", generator_.light_base);
}

float VirtualSensors::ReadTemperature() {
    // 温度在 20-35°C 范围内波动，±2°C
    float temp = AddNoise(generator_.temp_base, 2.0f);
    return Clamp(temp, 15.0f, 45.0f);
}

float VirtualSensors::ReadHumidity() {
    // 湿度在 40-60% 范围内波动，±5%
    float humidity = AddNoise(generator_.humidity_base, 5.0f);
    return Clamp(humidity, 30.0f, 80.0f);
}

float VirtualSensors::ReadLight() {
    // 光照在 30-70% 范围内波动，±10%
    float light = AddNoise(generator_.light_base, 10.0f);
    return Clamp(light, 10.0f, 100.0f);
}

VirtualSensorGenerator* VirtualSensors::GetGenerator() {
    return &generator_;
}
