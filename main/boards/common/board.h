#ifndef BOARD_H
#define BOARD_H

#include <http.h>
#include <web_socket.h>
#include <mqtt.h>
#include <udp.h>
#include <string>
#include <network_interface.h>
#include <driver/i2c_master.h>

#include "led/led.h"
#include "backlight.h"
#include "camera.h"

void* create_board();
class AudioCodec;
class Display;
class Board {
private:
    Board(const Board&) = delete; // 禁用拷贝构造函数
    Board& operator=(const Board&) = delete; // 禁用赋值操作

protected:
    Board();
    std::string GenerateUuid();

    // 软件生成的设备唯一标识
    std::string uuid_;

public:
    static Board& GetInstance() {
        static Board* instance = static_cast<Board*>(create_board());
        return *instance;
    }

    virtual ~Board() = default;
    virtual std::string GetBoardType() = 0;
    virtual std::string GetUuid() { return uuid_; }
    virtual Backlight* GetBacklight() { return nullptr; }
    virtual Led* GetLed();
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual bool GetTemperature(float& esp32temp);
    virtual Display* GetDisplay();
    virtual Camera* GetCamera();
    /**
     * @brief 获取板载共享 I2C 总线句柄
     * @details 板上多颗器件（PCA9557、ES8311/ES7210、FT63x6 触摸、QMI8658A IMU）共用同一条总线；
     *          各驱动应拿这个句柄按自己的地址加设备（i2c_master_bus_add_device），
     *          不要自行 i2c_new_master_bus（同一组引脚不能重复建总线）。
     * @return 总线句柄；不支持 I2C 的板子返回 nullptr
     */
    virtual i2c_master_bus_handle_t GetI2cBus() { return nullptr; }
    virtual NetworkInterface* GetNetwork() = 0;
    virtual void StartNetwork() = 0;
    virtual const char* GetNetworkStateIcon() = 0;
    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging);
    virtual std::string GetJson();
    virtual void SetPowerSaveMode(bool enabled) = 0;
    virtual std::string GetBoardJson() = 0;
    virtual std::string GetDeviceStatusJson() = 0;
};

#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { \
    return new BOARD_CLASS_NAME(); \
}

#endif // BOARD_H
