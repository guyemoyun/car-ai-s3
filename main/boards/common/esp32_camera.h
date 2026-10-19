#ifndef ESP32_CAMERA_H
#define ESP32_CAMERA_H

#include <esp_camera.h>
#include <lvgl.h>
#include <thread>
#include <memory>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class Esp32Camera : public Camera {
private:
    camera_fb_t* fb_ = nullptr;
    lv_img_dsc_t preview_image_;
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;
    std::mutex fb_mutex_;   // 串行化所有 fb 访问（预览任务与 Capture 不能同时取帧）

    /**
     * @brief 持锁把当前帧拷进调用方缓冲（GetPreviewFrame / GetRawFrame 的共用实现）
     * @param to_little_endian true=逐 16 位 bswap（供 LVGL）；false=原样（供 esp-dl）
     * @note 内部自行加 fb_mutex_ 锁
     */
    bool CopyFrameLocked(uint8_t* dst, size_t dst_size, int* out_w, int* out_h,
                         bool to_little_endian);

public:
    Esp32Camera(const camera_config_t& config);
    ~Esp32Camera();

    virtual void SetExplainUrl(const std::string& url, const std::string& token);
    virtual bool Capture();
    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::string Explain(const std::string& question);
    virtual bool GetPreviewFrame(uint8_t* dst, size_t dst_size, int* out_w, int* out_h) override;
    virtual bool GetRawFrame(uint8_t* dst, size_t dst_size, int* out_w, int* out_h) override;
};

#endif // ESP32_CAMERA_H