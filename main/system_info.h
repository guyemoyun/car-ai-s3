#ifndef _SYSTEM_INFO_H_
#define _SYSTEM_INFO_H_

#include <string>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>

class SystemInfo {
public:
    static size_t GetFlashSize();
    static size_t GetMinimumFreeHeapSize();
    static size_t GetFreeHeapSize();
    static std::string GetMacAddress();
    static std::string GetChipModelName();
    static std::string GetUserAgent();
    static esp_err_t PrintTaskCpuUsage(TickType_t xTicksToWait);
    static void PrintTaskList();
    static void PrintHeapStats();

    /**
     * @brief 打印内部 RAM / PSRAM 的逐块明细（诊断用，输出行数较多，建议只调一次）
     * @details 内部 RAM 才是本工程的真正瓶颈；本函数列出内部堆每个块的
     *          free / allocated / largest，据此定位"谁把内部 RAM 吃光了"
     */
    static void PrintHeapDetail();
};

#endif // _SYSTEM_INFO_H_
