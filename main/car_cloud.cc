/**
 * @file car_cloud.cc
 * @brief 巴法云上报实现（见 car_cloud.h 的设计说明）
 */

#include "car_cloud.h"

#include "board.h"          // Board::GetInstance().GetNetwork()
#include "car_monitor.h"    // CarMonitor::GetInstance()->GetSnapshot()
#include "car_monitor_helper.h"  // CarMonitorHelper::UpdateNetworkState()
#include "settings.h"       // NVS 凭据
#include "wifi_station.h"   // 本板为 Wi-Fi 板，用于如实上报链路状态

#include <mqtt.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <deque>
#include <memory>
#include <string>

#define TAG "CarCloud"

namespace {

// ---- 巴法云接入参数（官方文档：用户私钥当 clientId；普通端口 9501 / 加密 9503）----
constexpr const char* kHost = "bemfa.com";
// 本工程 Mqtt 封装只在 8883 启用 TLS，9503 用它连不上；且 TLS 会额外吃 30~50KB 内部 RAM，
// 本板内部 RAM 是唯一瓶颈 → 固定用 9501 普通端口（明文，仅上报监控数据）。
constexpr int kPort = 9501;
// 联调用的私钥兜底值：优先读 NVS（命名空间 car_cloud，键 uid）。正式版请移入 NVS 并删除本行。
constexpr const char* kUidFallback = "7ea6e057c5f2484a83a9c5aa20ed7f25";
// 主题前缀：可选，**只能含字母和数字**（巴法云限制："主题只能包含字母和数字"），
// 默认空前缀 → 主题名就是 env / state / alarm / online；
// 若要多设备区分，可用 NVS 键 car_cloud/prefix 设成如 "carmon" → carmonenv / carmonstate / …
constexpr const char* kPrefixFallback = "";

constexpr uint32_t kTaskPeriodMs = 1000;       // 任务周期：告警变化在 ≤1s 内上报
constexpr uint32_t kReconnectRetryMs = 60000;  // 两次"主动 Connect"之间的最小间隔（其余交给 esp-mqtt 自动重连）
constexpr uint32_t kRebuildAfterMs = 180000;   // 已连过但连续断线超过这个时长，才允许销毁重建兜底一次
constexpr int kAlertCacheMax = 32;             // 断线期间最多缓存的告警事件数
constexpr int kKeepAliveSeconds = 120;

TaskHandle_t s_task = nullptr;
uint32_t s_state_interval_ms = 5000;
uint32_t s_online_interval_ms = 30000;

// 供 IsConnected() 查询；只写一个 bool，读写竞争无风险（状态量，不是数据）
bool s_connected = false;

uint32_t NowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

/**
 * @brief 把浮点数值写成一位小数字符串
 * @note 本工程 newlib 为 nano 格式，`%f` / cJSON 的 `%g` 都不可靠，故一律用整数运算拼文本。
 */
void FormatFixed1(char* out, size_t out_size, float value) {
    int v10 = (int)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    if (v10 < 0) {
        snprintf(out, out_size, "-%d.%d", (-v10) / 10, (-v10) % 10);
    } else {
        snprintf(out, out_size, "%d.%d", v10 / 10, v10 % 10);
    }
}

/** @brief 私钥只打前后各几位，日志不落完整凭据 */
std::string MaskSecret(const std::string& value) {
    if (value.size() <= 10) {
        return std::string("******");
    }
    return value.substr(0, 4) + "****" + value.substr(value.size() - 4);
}

const char* JsonBool(bool value) { return value ? "true" : "false"; }

const char* VehicleName(CarVehicleState state) {
    switch (state) {
        case CAR_STATE_RUNNING: return "RUNNING";
        case CAR_STATE_PARKED:  return "PARKED";
        default:                return "UNKNOWN";
    }
}

const char* PersonName(CarPersonState state) {
    switch (state) {
        case PERSON_STATE_EMPTY:   return "EMPTY";
        case PERSON_STATE_PRESENT: return "PRESENT";
        default:                   return "UNKNOWN";
    }
}

const char* AlertName(AlertType type) {
    switch (type) {
        case ALERT_PERSON_LEFT:    return "person_left";
        case ALERT_TEMP_HIGH:      return "temp_high";
        case ALERT_HUMIDITY_HIGH:  return "humidity_high";
        case ALERT_LIGHT_ABNORMAL: return "light_abnormal";
        default:                   return "none";
    }
}

/**
 * @brief 组装环境主题负载；数值失效时该字段发 null（计划要求"失效发 null"）
 */
std::string BuildEnvJson(const CarSnapshot& s) {
    char temp[16];
    char hum[16];
    char light[16];
    FormatFixed1(temp, sizeof(temp), s.env_data.temperature);
    FormatFixed1(hum, sizeof(hum), s.env_data.humidity);
    FormatFixed1(light, sizeof(light), s.env_data.light_percent);

    char buf[384];
    snprintf(buf, sizeof(buf),
             "{\"v\":1,\"ts\":%u,\"demo\":%s,"
             "\"temp\":%s,\"temp_ok\":%s,\"temp_age\":%u,"
             "\"hum\":%s,\"hum_ok\":%s,\"hum_age\":%u,"
             "\"light\":%s,\"light_ok\":%s,\"light_age\":%u}",
             (unsigned)NowMs(), JsonBool(s.demo_mode),
             s.env_data.temperature_valid ? temp : "null",
             JsonBool(s.env_data.temperature_valid), (unsigned)s.env_data.temperature_age_ms,
             s.env_data.humidity_valid ? hum : "null",
             JsonBool(s.env_data.humidity_valid), (unsigned)s.env_data.humidity_age_ms,
             s.env_data.light_valid ? light : "null",
             JsonBool(s.env_data.light_valid), (unsigned)s.env_data.light_age_ms);
    return std::string(buf);
}

/**
 * @brief 组装状态主题负载：车辆 / 人员 / 运动 / 告警 / 故障 / 网络
 * @note 负载刻意压到 ~180 字节：实测 env（~165B）能到达控制台，而原来 ~450B 且含嵌套对象的
 *       state 一直收不到，判断是巴法云对单条消息长度有限制 → 这里去掉冗余字段、
 *       把故障标志压成一个短字符串（如 "cam,imu" / "none"），布尔用 0/1。
 * @note wifi 与 cloud 两个字段由本模块每秒写入快照（CarMonitorHelper::UpdateNetworkState），
 *       因此这里读到的是真实链路状态，而不是没人写过、恒为 false 的默认值。
 */
std::string BuildStateJson(const CarSnapshot& s, bool cloud_link_ok) {
    char conf[16];
    FormatFixed1(conf, sizeof(conf), s.person_data.confidence);

    // 故障标志压成 "temp,hum,cam,audio,i2c,imu" 的子集，全无故障时为 "none"
    const char* fault_names[6] = {"temp", "hum", "cam", "audio", "i2c", "imu"};
    const bool fault_on[6] = {s.temp_sensor_fault, s.humidity_sensor_fault, s.camera_fault,
                              s.audio_fault, s.i2c_fault, s.imu_fault};
    char fault[48];
    size_t fault_len = 0;
    snprintf(fault, sizeof(fault), "none");
    for (int i = 0; i < 6; i++) {
        if (!fault_on[i]) {
            continue;
        }
        if (fault_len == 0) {
            fault[0] = '\0';   // 首个故障进来时清掉占位的 "none"
        }
        fault_len += (size_t)snprintf(fault + fault_len, sizeof(fault) - fault_len, "%s%s",
                                      (fault_len > 0) ? "," : "", fault_names[i]);
        if (fault_len >= sizeof(fault)) {
            fault_len = sizeof(fault) - 1;
            break;
        }
    }

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"v\":1,\"ts\":%u,\"veh\":\"%s\",\"person\":\"%s\",\"cnt\":%d,\"conf\":%s,"
             "\"moving\":%d,\"imu_ok\":%d,\"alert\":\"%s\",\"alert_ok\":%d,"
             "\"fault\":\"%s\",\"wifi\":%d,\"cloud\":%d,\"demo\":%d}",
             (unsigned)NowMs(), VehicleName(s.vehicle_state),
             PersonName(s.person_data.state), s.person_data.person_count, conf,
             (int)s.is_moving, (int)!s.imu_fault, AlertName(s.active_alert), (int)s.alert_confirmed,
             fault, (int)s.wifi_connected, (int)cloud_link_ok, (int)s.demo_mode);
    return std::string(buf);
}

/** @brief 组装在线心跳负载 */
std::string BuildOnlineJson(uint32_t boot_id, uint32_t uptime_s) {
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"v\":1,\"ts\":%u,\"boot\":%u,\"uptime_s\":%u}",
             (unsigned)NowMs(), (unsigned)boot_id, (unsigned)uptime_s);
    return std::string(buf);
}

/** @brief 组装告警事件负载（发生 / 确认 / 解除） */
std::string BuildAlertJson(uint32_t seq, const char* event, const char* type) {
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"v\":1,\"ts\":%u,\"seq\":%u,\"event\":\"%s\",\"type\":\"%s\"}",
             (unsigned)NowMs(), (unsigned)seq, event, type);
    return std::string(buf);
}

/**
 * @brief 发布一条告警事件；未连接则入有界队列等待补发
 * @note 用 `/set` 后缀：巴法云对 `/set` 会主动推送给订阅者（告警要"响"），
 *       周期数据用 `/up`（只更新云端，不打扰订阅者）
 */
void PublishAlert(Mqtt* mqtt, bool connected, const std::string& topic_alert,
                  std::deque<std::string>& cache, uint32_t seq, const char* event, const char* type) {
    const std::string payload = BuildAlertJson(seq, event, type);
    if (connected && mqtt->Publish(topic_alert + "/set", payload, 1)) {
        ESP_LOGW(TAG, "告警事件已上报：%s %s", event, type);
        return;
    }
    ESP_LOGW(TAG, "告警事件未能上报（离线），入队等待补发：%s %s", event, type);
    if ((int)cache.size() >= kAlertCacheMax) {
        cache.pop_front();  // 有界：丢最旧，保最新
    }
    cache.push_back(payload);
}

}  // namespace

bool CarCloud::Start(uint32_t state_interval_ms, uint32_t online_interval_ms) {
    if (s_task != nullptr) {
        ESP_LOGW(TAG, "云上报任务已在运行");
        return false;
    }
    s_state_interval_ms = state_interval_ms;
    s_online_interval_ms = online_interval_ms;

    // 钉 CPU0 / 优先级 2：低于预览(3)，高于人员检测(1)；不占 CPU1（音频/AFE/wakenet 所在核）
    // 栈取 2560B（原先 4096B）：本板内部 RAM 是唯一瓶颈，云上报只做 JSON 拼装 + publish，不需要更大栈
    BaseType_t ret = xTaskCreatePinnedToCore(TaskFunc, "car_cloud", 2560, nullptr, /*优先级*/ 2,
                                             &s_task, /*CPU0*/ 0);
    if (ret != pdPASS) {
        s_task = nullptr;
        ESP_LOGE(TAG, "云上报任务创建失败");
        return false;
    }
    ESP_LOGI(TAG, "云上报任务已启动（状态 %u ms / 心跳 %u ms）",
             (unsigned)state_interval_ms, (unsigned)online_interval_ms);
    return true;
}

void CarCloud::Stop() {
    if (s_task != nullptr) {
        vTaskDelete(s_task);
        s_task = nullptr;
        s_connected = false;
        ESP_LOGW(TAG, "云上报任务已停止");
    }
}

bool CarCloud::IsRunning() {
    return s_task != nullptr;
}

bool CarCloud::IsConnected() {
    return s_connected;
}

void CarCloud::TaskFunc(void* arg) {
    (void)arg;

    // ---- 1) 凭据与主题前缀 ----
    Settings settings_ro("car_cloud", false);
    const std::string uid = settings_ro.GetString("uid", kUidFallback);
    // 前缀只能含字母数字（巴法云限制），默认空 → 主题名就是 env / state / alarm / online
    const std::string prefix = settings_ro.GetString("prefix", kPrefixFallback);

    if (uid.empty()) {
        ESP_LOGW(TAG, "未配置巴法云用户私钥（NVS car_cloud/uid），云上报不启动；其余功能不受影响");
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // boot_id：NVS 自增，用于区分上电周期（计划要求上报 boot 标识）
    uint32_t boot_id = 1;
    {
        Settings settings_rw("car_cloud", true);
        boot_id = (uint32_t)(settings_rw.GetInt("boot_id", 0) + 1);
        settings_rw.SetInt("boot_id", (int32_t)boot_id);
    }

    // 主题名只能用字母和数字（巴法云限制），故不用下划线分隔；告警主题直接复用已建的 alarm
    const std::string topic_env = prefix + "env";
    const std::string topic_state = prefix + "state";
    const std::string topic_alert = prefix + "alarm";
    const std::string topic_online = prefix + "online";

    ESP_LOGI(TAG, "巴法云上报启动：%s:%d clientId=%s boot=%u",
             kHost, kPort, MaskSecret(uid).c_str(), (unsigned)boot_id);
    ESP_LOGI(TAG, "请在巴法云控制台创建这 4 个主题：%s / %s / %s / %s",
             topic_env.c_str(), topic_state.c_str(), topic_alert.c_str(), topic_online.c_str());

    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        ESP_LOGE(TAG, "没有网络接口，云上报不启动");
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    std::unique_ptr<Mqtt> mqtt = network->CreateMqtt(0);
    if (mqtt == nullptr) {
        ESP_LOGE(TAG, "创建 MQTT 客户端失败，云上报不启动");
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    mqtt->SetKeepAlive(kKeepAliveSeconds);
    mqtt->OnDisconnected([]() { ESP_LOGW(TAG, "巴法云连接断开（等待自动重连）"); });
    mqtt->OnError([](const std::string& error) { ESP_LOGW(TAG, "巴法云连接异常：%s", error.c_str()); });

    CarMonitor* monitor = CarMonitor::GetInstance();
    if (monitor == nullptr) {
        ESP_LOGE(TAG, "CarMonitor 未初始化，云上报不启动");
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    std::deque<std::string> alert_cache;   // 断线期间的告警事件（有界 32）
    AlertType last_alert = ALERT_NONE;
    bool last_confirmed = false;
    uint32_t alert_seq = 0;
    uint32_t tick = 0;
    const int64_t started_us = esp_timer_get_time();
    bool ever_connected = false;          // 是否成功连上过（决定首次重试策略）
    bool logged_size = false;             // 是否已打印过负载长度（诊断用，只打一次）
    int64_t last_connected_us = -1;       // 最近一次"处于已连接"的时刻
    // 首次进入循环即尝试连接：初值取负值，保证 now - last >= 尝试间隔
    int64_t last_connect_try_us = -((int64_t)kReconnectRetryMs * 1000);

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        // ---- 2) 链路状态 ----
        // 与参考例程（02-network/06、07 的 mqtt_demo.c）保持同一姿势：
        //   esp_mqtt_client_init + start 之后**绝不反复销毁重建**，断开交给 esp-mqtt 自身自动重连；
        //   连接建立时订阅自己的主题。
        // 原因：本工程的 Mqtt::Connect() 会先 Disconnect()（销毁客户端）再重建，若每 60s 调一次，
        // 等于用同一个 clientId（用户私钥）反复向巴法云发起连接，容易被 broker 拒/踢，
        // 表现为"连上几分钟后被 RST，此后一直起不来"。所以这里只在下面两种情况才 Connect：
        //   ① 从未连上过（首次）；② 已连过但连续断线超过 kRebuildAfterMs（才允许重建兜底）。
        const int64_t now_us = esp_timer_get_time();
        const bool wifi_ok = WifiStation::GetInstance().IsConnected();
        const bool mqtt_connected = mqtt->IsConnected();

        if (mqtt_connected) {
            last_connected_us = now_us;
        }

        if (!mqtt_connected && wifi_ok &&
            (now_us - last_connect_try_us) >= (int64_t)kReconnectRetryMs * 1000) {
            const bool never_connected = !ever_connected;
            const bool stuck_too_long =
                (last_connected_us >= 0) && ((now_us - last_connected_us) > (int64_t)kRebuildAfterMs * 1000);
            if (never_connected || stuck_too_long) {
                last_connect_try_us = now_us;
                ESP_LOGI(TAG, "连接巴法云 %s:%d …（%s）", kHost, kPort,
                         never_connected ? "首次" : "长时间断线后重建");
                // 注意：Connect() 内部最多阻塞 10s 等连接事件
                if (mqtt->Connect(kHost, kPort, uid, "", "")) {
                    ever_connected = true;
                    ESP_LOGI(TAG, "巴法云已连接");
                } else {
                    ESP_LOGW(TAG, "巴法云连接失败（交由 esp-mqtt 自动重连，%u s 后再兜底）",
                             (unsigned)(kReconnectRetryMs / 1000));
                }
            }
        }

        // 连接建立（含自动重连成功）的那一拍：订阅 4 个主题（例程同样是"连上就订阅"）
        if (mqtt_connected && !s_connected) {
            ESP_LOGI(TAG, "巴法云链路就绪，订阅主题：%s %s %s %s",
                     topic_env.c_str(), topic_state.c_str(), topic_alert.c_str(), topic_online.c_str());
            mqtt->Subscribe(topic_env, 0);
            mqtt->Subscribe(topic_state, 0);
            mqtt->Subscribe(topic_alert, 0);
            mqtt->Subscribe(topic_online, 0);
        }
        s_connected = mqtt_connected;
        CarMonitorHelper::UpdateNetworkState(wifi_ok, s_connected);

        // ---- 3) 断线补发（有界队列，先进先出）----
        if (s_connected && !alert_cache.empty()) {
            ESP_LOGI(TAG, "补发断线期间缓存的 %u 条告警事件", (unsigned)alert_cache.size());
            while (!alert_cache.empty()) {
                mqtt->Publish(topic_alert + "/set", alert_cache.front(), 1);
                alert_cache.pop_front();
            }
        }

        // ---- 4) 读一致快照 ----
        CarSnapshot s;
        const bool snap_ok = monitor->GetSnapshot(&s);

        // ---- 5) 告警事件：变化即上报（≤1s）----
        if (snap_ok) {
            const AlertType current = s.active_alert;
            if (current != last_alert) {
                if (last_alert != ALERT_NONE) {
                    PublishAlert(mqtt.get(), s_connected, topic_alert, alert_cache,
                                 ++alert_seq, "clear", AlertName(last_alert));
                }
                if (current != ALERT_NONE) {
                    PublishAlert(mqtt.get(), s_connected, topic_alert, alert_cache,
                                 ++alert_seq, "raise", AlertName(current));
                }
                last_alert = current;
                last_confirmed = false;
            } else if (current != ALERT_NONE && s.alert_confirmed && !last_confirmed) {
                PublishAlert(mqtt.get(), s_connected, topic_alert, alert_cache,
                             ++alert_seq, "confirm", AlertName(current));
                last_confirmed = true;
            }
        }

        // ---- 6) 周期上报：环境/状态 5s，在线心跳 30s ----
        ++tick;
        const uint32_t state_ticks = (s_state_interval_ms >= kTaskPeriodMs) ? (s_state_interval_ms / kTaskPeriodMs) : 1;
        const uint32_t online_ticks = (s_online_interval_ms >= kTaskPeriodMs) ? (s_online_interval_ms / kTaskPeriodMs) : 1;

        if (s_connected && snap_ok && (tick % state_ticks == 0)) {
            const std::string env_payload = BuildEnvJson(s);
            const std::string state_payload = BuildStateJson(s, s_connected);
            if (!logged_size) {  // 只打一次，便于核对是否触到巴法云的单条消息长度上限
                logged_size = true;
                ESP_LOGI(TAG, "上报负载长度：env=%u B，state=%u B",
                         (unsigned)env_payload.size(), (unsigned)state_payload.size());
            }
            // 用 /up：只更新云端数据、不做推送。
            // 依据：控制台里 online 卡片显示的正是我们 /up 发出去的负载，说明 /up 同样可见；
            // 周期数据不需要推送给订阅者，也少一层被限流的风险。
            const bool env_ok = mqtt->Publish(topic_env + "/up", env_payload, 0);
            const bool state_ok = mqtt->Publish(topic_state + "/up", state_payload, 0);
            if (!env_ok || !state_ok) {
                ESP_LOGW(TAG, "上报调用返回失败（env=%d state=%d，state=%u B）",
                         (int)env_ok, (int)state_ok, (unsigned)state_payload.size());
            }
        }
        if (s_connected && (tick % online_ticks == 0)) {
            const uint32_t uptime_s = (uint32_t)((esp_timer_get_time() - started_us) / 1000000);
            mqtt->Publish(topic_online + "/up", BuildOnlineJson(boot_id, uptime_s), 0);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kTaskPeriodMs));
    }
}
