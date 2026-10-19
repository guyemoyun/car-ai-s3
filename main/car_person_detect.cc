/**
 * @file car_person_detect.cc
 * @brief 本地人员检测（M4）实现：摄像头原始帧 → esp-dl 推理 → 上报车载监测
 *
 * 设计要点（详见 .trae/documents/M4-人员识别-检测到告警链路-方案.md）：
 *  - 取帧走 Camera::GetRawFrame()（原样 RGB565 大端，不转字节序），
 *    esp-dl 的 ImagePreprocessor 自行完成 resize + 色彩转换 + 归一化量化；
 *  - 任务钉 CPU0 / 优先级 1（低于预览 3、car_monitor 5），不抢 CPU1 的音频；
 *  - esp-dl 张量池默认全在 PSRAM，内部 RAM 增量仅为本任务栈；
 *  - 任何失败都**不上报**，避免把"连续有人计数"清零造成假无人；模型问题只记故障。
 */

#include "car_person_detect.h"

#include "application.h"         // Application::GetInstance().GetDeviceState()
#include "board.h"               // Board::GetInstance().GetCamera()
#include "camera.h"              // Camera::GetRawFrame()
#include "car_monitor_helper.h"  // CarMonitorHelper::OnPersonDetectionResult()
#include "system_info.h"         // SystemInfo::PrintHeapDetail()（诊断）

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <list>
#include <new>

#include "pedestrian_detect.hpp"  // espressif/pedestrian_detect（PICO_S8_V1）

#define TAG "PersonDetect"

namespace {

// 与摄像头 QVGA 输出一致（见 grw_esp32s3_board.cc 的 FRAMESIZE_QVGA）
constexpr int kFrameW = 320;
constexpr int kFrameH = 240;
constexpr size_t kFrameBytes = (size_t)kFrameW * kFrameH * 2;  // 153,600

// 模型分区（须与 partitions/v1/16m.csv 中的名字一致）
constexpr const char* kModelPartition = "pedestrian_det";
constexpr size_t kModelMinSize = 512 * 1024;  // 模型约 425 KiB，留足余量

// 连续失败这么多次后尝试重建 detector（2Hz × 60 ≈ 30 s）
constexpr int kRebuildAfterFailures = 60;

// 开发期诊断开关：true = 加载模型后打印 esp-dl 逐层内存(profile_memory) + 堆逐块明细，
// 输出行数较多，仅诊断期开；发布前置 false。
constexpr bool kDumpModelMemory = true;

// 诊断开关：false = 完全不启动人员检测任务。
// 用途：二分定位"触摸不灵是否由本任务（推理占用 CPU0/PSRAM 带宽）引起"——
// 置 false 烧一次，触摸若恢复灵敏即确认是争抢；仍不灵则问题在触摸 IC/驱动/硬件。
// 【当前状态：true —— 触摸已改用中断驱动（CTP_INT=IO10）从结构上解决漏采，无需再关检测】
constexpr bool kEnablePersonDetect = true;

TaskHandle_t s_task = nullptr;
uint8_t* s_frame_buf = nullptr;         // PSRAM，153,600 B
PedestrianDetect* s_det = nullptr;
uint32_t s_interval_ms = 500;
uint32_t s_chat_interval_ms = 1000;
int s_fail_streak = 0;
bool s_fault = false;                   // true=已判定不可恢复，不再重建

/** @brief 当前是否处于小智会话（非空闲/未知即视为会话中） */
bool InChatSession() {
    switch (Application::GetInstance().GetDeviceState()) {
        case kDeviceStateIdle:
        case kDeviceStateUnknown:
            return false;
        default:
            return true;
    }
}

/** @brief 校验模型分区存在且足够大 */
bool ModelPartitionOk() {
    const esp_partition_t* p = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, kModelPartition);
    if (p == nullptr) {
        ESP_LOGE(TAG, "模型分区 '%s' 不存在（分区表需含该行并重新 idf.py flash）", kModelPartition);
        return false;
    }
    if (p->size < kModelMinSize) {
        ESP_LOGE(TAG, "模型分区 '%s' 太小：%u < %u", kModelPartition,
                 (unsigned)p->size, (unsigned)kModelMinSize);
        return false;
    }
    return true;
}

/** @brief 创建（或重建）detector；失败返回 false */
bool CreateDetector() {
    if (s_det != nullptr) {
        delete s_det;
        s_det = nullptr;
    }
    const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    // 用 eager 加载（lazy_load=false）：这样 get_raw_model() 立刻可判，
    // 能在第一次 run() 之前就把"模型不可用"拦下来，避免空指针解引用崩溃。
    // 构造/加载在本任务栈（4096）内完成，不动用调用方栈。
    s_det = new (std::nothrow) PedestrianDetect(PedestrianDetect::PICO_S8_V1, /*lazy_load=*/false);
    if (s_det == nullptr || s_det->get_raw_model(0) == nullptr) {
        ESP_LOGE(TAG, "模型加载失败（分区内容为空或损坏？）");
        delete s_det;
        s_det = nullptr;
        return false;
    }

    s_det->set_score_thr(0.7f, 0);  // 官方默认；误报高再往上调
    s_det->set_nms_thr(0.5f, 0);

    const size_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "模型加载完成：PSRAM 占用 %u 字节 | 内部 RAM %u -> %u（本次占用 %d，历史最低 %u）",
             (unsigned)((psram_before > psram_after) ? (psram_before - psram_after) : 0),
             (unsigned)internal_before, (unsigned)internal_after,
             (int)internal_before - (int)internal_after,
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

    if (kDumpModelMemory) {
        s_det->get_raw_model(0)->profile_memory();  // esp-dl 逐层内存（PSRAM/内部拆分）
        SystemInfo::PrintHeapDetail();              // 堆逐块明细
    }
    return true;
}

}  // namespace

bool CarPersonDetect::Start(uint32_t interval_ms, uint32_t chat_interval_ms) {
    if (!kEnablePersonDetect) {
        ESP_LOGW(TAG, "人员检测被 kEnablePersonDetect=false 关闭（诊断模式，不启动任务）");
        return false;
    }
    if (s_task != nullptr) {
        ESP_LOGW(TAG, "人员检测任务已在运行");
        return false;
    }
    s_interval_ms = interval_ms;
    s_chat_interval_ms = chat_interval_ms;
    s_fail_streak = 0;
    s_fault = false;

    BaseType_t ret = xTaskCreatePinnedToCore(TaskFunc, "person_detect", 4096, nullptr, 1,
                                             &s_task, /*CPU0*/ 0);
    if (ret != pdPASS) {
        s_task = nullptr;
        ESP_LOGE(TAG, "人员检测任务创建失败");
        return false;
    }
    ESP_LOGI(TAG, "人员检测任务已启动（窗口 %u ms，会话期 %u ms，CPU0/优先级1）",
             (unsigned)interval_ms, (unsigned)chat_interval_ms);
    return true;
}

void CarPersonDetect::Stop() {
    if (s_task != nullptr) {
        vTaskDelete(s_task);
        s_task = nullptr;
        ESP_LOGW(TAG, "人员检测任务已停止");
    }
}

bool CarPersonDetect::IsRunning() {
    return s_task != nullptr;
}

void CarPersonDetect::TaskFunc(void* arg) {
    (void)arg;
    ESP_LOGI(TAG, "Person detect task running");

    Camera* camera = Board::GetInstance().GetCamera();
    if (camera == nullptr) {
        ESP_LOGE(TAG, "没有可用摄像头，人员检测不启动");
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // 前置检查：分区与帧缓冲
    if (!ModelPartitionOk()) {
        s_fault = true;
    }
    if (!s_fault && s_frame_buf == nullptr) {
        s_frame_buf = (uint8_t*)heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM);
        if (s_frame_buf == nullptr) {
            ESP_LOGE(TAG, "识别帧缓冲分配失败（%u 字节，PSRAM）", (unsigned)kFrameBytes);
            s_fault = true;
        }
    }
    if (s_fault || !CreateDetector()) {
        s_fault = true;
        ESP_LOGE(TAG, "人员检测不可用 —— 不上报任何结果（避免假无人），预览与音频不受影响");
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "启动时内部 RAM free=%u，栈余量=%u",
             (unsigned)internal_before,
             (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));

    TickType_t last_wake = xTaskGetTickCount();
    int log_div = 0;
    int report_div = 0;

    while (true) {
        const uint32_t interval = InChatSession() ? s_chat_interval_ms : s_interval_ms;

        bool got = false;
        int w = 0;
        int h = 0;
        int64_t t0 = esp_timer_get_time();

        if (s_det != nullptr) {
            got = camera->GetRawFrame(s_frame_buf, kFrameBytes, &w, &h);
        }

        if (got && w == kFrameW && h == kFrameH) {
            dl::image::img_t img = {};
            img.data = s_frame_buf;
            img.width = (uint16_t)kFrameW;
            img.height = (uint16_t)kFrameH;
            img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;

            // run() 返回内部列表引用，不得跨帧持有；run() 非线程安全，故只在此外调用
            std::list<dl::detect::result_t>& res = s_det->run(img);
            int64_t t1 = esp_timer_get_time();

            int count = 0;
            float best = 0.0f;
            for (const auto& r : res) {
                count++;
                if (r.score > best) {
                    best = r.score;
                }
            }

            // 复用既有唯一上报入口（内部会更新 car_monitor 的 person_data）
            CarMonitorHelper::OnPersonDetectionResult(count > 0 ? 1 : 0, best);
            s_fail_streak = 0;

            const int best_x100 = (int)(best * 100.0f + 0.5f);
            const int infer_ms = (int)((t1 - t0) / 1000);
            if (++log_div >= 5) {  // 约每 2.5 s 一行，避免刷屏
                log_div = 0;
                // 注意：本工程 newlib 为 nano 格式，%f / %lld 不可靠，只用整数格式
                ESP_LOGI(TAG, "检测：人数=%d 最高分=%d.%02d 耗时=%d ms", count,
                         best_x100 / 100, best_x100 % 100, infer_ms);
            }
            if (++report_div >= 20) {  // 约每 10 s 汇报一次资源
                report_div = 0;
                ESP_LOGI(TAG, "资源：内部 RAM free=%u 栈余量=%u",
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                         (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
            }
        } else {
            // 取帧失败或尺寸不符：不上报（否则会清零"连续有人计数"）
            if (got) {
                ESP_LOGW(TAG, "帧尺寸异常：%dx%d（期望 %dx%d）", w, h, kFrameW, kFrameH);
            }
            if (!s_fault && ++s_fail_streak >= kRebuildAfterFailures) {
                ESP_LOGW(TAG, "连续 %d 次异常，尝试重建 detector", s_fail_streak);
                if (!CreateDetector()) {
                    s_fault = true;
                    ESP_LOGE(TAG, "重建失败，停止人员检测（预览/音频不受影响）");
                    break;
                }
                s_fail_streak = 0;
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(interval));
    }

    s_task = nullptr;
    vTaskDelete(nullptr);
}
