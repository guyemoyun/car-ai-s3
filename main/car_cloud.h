/**
 * @file car_cloud.h
 * @brief 巴法云（bemfa.com）上报：独立 MQTT 客户端 + 周期上报 + 告警事件有界补发
 *
 * 设计要点：
 *  - 与 M4/M5 链路完全解耦：只读 CarMonitor 的一致快照（GetSnapshot），不改任何业务状态；
 *  - 走 **9501 普通端口（非 TLS）**：本板内部 RAM 紧张（空闲约 47KB），TLS 会额外吃 30~50KB；
 *    而且本工程的 Mqtt 封装只在端口 8883 才启用 TLS，用 9503 反而连不上；
 *  - 鉴权用巴法云"方式一"：clientId = 用户私钥，username/password 留空（官方文档）；
 *  - 主题名**只能含字母和数字**（巴法云新建主题时会校验"主题只能包含字母和数字"），
 *    故不加下划线分隔，默认就是 `env` / `state` / `alarm` / `online`；
 *    如需多设备区分，可用 NVS `car_cloud/prefix`（同样只能字母数字）拼在前面；
 *  - 新任务只做上报：周期 1s 检查，环境/状态每 5s 上报一次，线上心跳每 30s 一次，
 *    告警变化 ≤1s 内上报；断线期间的告警事件最多缓存 32 条，重连后补发；
 *  - 凭据来源：NVS 命名空间 `car_cloud` 的 `uid`（缺省回落到本文件内的联调常量）；
 *  - 任何失败都只降级，不阻塞采集、界面、语音与告警。
 */

#pragma once

#include <cstdint>

class CarCloud {
public:
    /**
     * @brief 启动云上报任务
     * @param state_interval_ms  环境/状态上报周期（默认 5000ms）
     * @param online_interval_ms 在线心跳周期（默认 30000ms）
     * @return true=任务已创建；false=已在运行、未配置私钥或任务创建失败
     * @note 任务钉 CPU0 / 优先级 2 / 栈 2560 字节：低于预览(3)，不与音频所在 CPU1 抢核
     */
    static bool Start(uint32_t state_interval_ms = 5000, uint32_t online_interval_ms = 30000);

    /// 停止上报任务（诊断用）
    static void Stop();

    /// 任务是否在运行
    static bool IsRunning();

    /// 巴法云 MQTT 是否已连接
    static bool IsConnected();

private:
    static void TaskFunc(void* arg);
};
