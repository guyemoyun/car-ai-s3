#ifndef CAMERA_H
#define CAMERA_H

#include <string>
#include <cstddef>
#include <cstdint>

class Camera {
public:
    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual std::string Explain(const std::string& question) = 0;

    /**
     * @brief 取一帧用于本地预览（RGB565，字节序已转为 LVGL 小端）
     * @param dst      目标缓冲（调用方提供，务必放 PSRAM，内部 RAM 放不下）
     * @param dst_size 目标缓冲字节数
     * @param out_w    输出：帧宽（可为 nullptr）
     * @param out_h    输出：帧高（可为 nullptr）
     * @return true=成功写入一帧；false=不支持预览或本次无可用帧
     * @note 默认返回 false，不支持预览的摄像头实现可忽略本接口
     */
    virtual bool GetPreviewFrame(uint8_t* dst, size_t dst_size, int* out_w, int* out_h) {
        (void)dst; (void)dst_size; (void)out_w; (void)out_h;
        return false;
    }

    /**
     * @brief 取一帧**原始** RGB565（字节序原样，不做转换），供本地推理使用
     * @param dst      目标缓冲（调用方提供，务必放 PSRAM）
     * @param dst_size 目标缓冲字节数
     * @param out_w    输出：帧宽（可为 nullptr）
     * @param out_h    输出：帧高（可为 nullptr）
     * @return true=成功写入一帧；false=不支持或本次无可用帧
     * @note 与 GetPreviewFrame 的唯一区别是不转字节序：esp-dl 的 ImagePreprocessor
     *       直接吃 RGB565 大端，自行完成 resize + 色彩转换 + 归一化量化
     */
    virtual bool GetRawFrame(uint8_t* dst, size_t dst_size, int* out_w, int* out_h) {
        (void)dst; (void)dst_size; (void)out_w; (void)out_h;
        return false;
    }
};

#endif // CAMERA_H
