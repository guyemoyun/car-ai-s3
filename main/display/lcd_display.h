/**
 * @file lcd_display.h
 * @brief LCD显示器驱动头文件
 * 
 * @details
 * 本文件定义了LcdDisplay类及其派生类，提供了完整的LCD显示功能接口。
 * 支持多种LCD接口类型：SPI、RGB、MIPI、QSPI、MCU8080。
 * 
 * 核心功能：
 * - UI界面管理（状态栏、聊天区域、侧边栏）
 * - 主题管理（深色/浅色主题切换）
 * - 聊天消息显示（用户/助手/系统消息）
 * - 表情图标显示
 * - 预览图片显示
 * 
 * 类继承关系：
 * Display (基类)
 *   └── LcdDisplay (LCD显示器基类)
 *         ├── SpiLcdDisplay (SPI接口LCD)
 *         ├── RgbLcdDisplay (RGB接口LCD)
 *         ├── MipiLcdDisplay (MIPI接口LCD)
 *         ├── QspiLcdDisplay (QSPI接口LCD)
 *         └── Mcu8080LcdDisplay (MCU8080接口LCD)
 * 
 * 使用方法：
 * 1. 包含头文件：#include "lcd_display.h"
 * 2. 根据硬件接口创建对应的显示器实例
 * 3. 调用SetChatMessage()显示聊天消息
 * 4. 调用SetEmotion()设置表情图标
 * 5. 调用SetStatus()设置状态栏文本
 * 6. 调用SetTheme()切换主题
 * 
 * @note 本文件依赖于LVGL图形库
 * @see lcd_display.cc 实现文件
 */

#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <font_emoji.h>

#include <atomic>
#include <vector>

// 主题颜色结构体
struct ThemeColors {
    lv_color_t background;      // 背景色
    lv_color_t text;            // 文本颜色
    lv_color_t chat_background;  // 聊天区域背景色
    lv_color_t user_bubble;     // 用户消息气泡颜色
    lv_color_t assistant_bubble; // 助手消息气泡颜色
    lv_color_t system_bubble;   // 系统消息气泡颜色
    lv_color_t system_text;     // 系统消息文本颜色
    lv_color_t border;          // 边框颜色
    lv_color_t low_battery;     // 低电量提示颜色
};


class LcdDisplay : public Display {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;  // LCD面板IO句柄
    esp_lcd_panel_handle_t panel_ = nullptr;        // LCD面板句柄
    
    lv_draw_buf_t draw_buf_;        // LVGL绘图缓冲区
    lv_obj_t* status_bar_ = nullptr;   // 状态栏对象
    lv_obj_t* content_ = nullptr;      // 内容区域对象
    lv_obj_t* container_ = nullptr;     // 容器对象
    lv_obj_t* side_bar_ = nullptr;      // 侧边栏对象
    lv_obj_t* preview_image_ = nullptr; // 预览图片对象

    DisplayFonts fonts_;          // 字体集合
    ThemeColors current_theme_;   // 当前主题颜色

    void SetupUI();  // 设置UI界面
    virtual bool Lock(int timeout_ms = 0) override;   // 锁定显示
    virtual void Unlock() override;                   // 解锁显示

protected:
    // 添加protected构造函数
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, DisplayFonts fonts, int width, int height);
    
public:
    ~LcdDisplay();  // 析构函数
    virtual void SetEmotion(const char* emotion) override;  // 设置表情图标
    virtual void SetIcon(const char* icon) override;      // 设置图标
    virtual void SetPreviewImage(const lv_img_dsc_t* img_dsc) override;  // 设置预览图片
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    virtual void SetChatMessage(const char* role, const char* content) override;  // 设置聊天消息
#endif  

    // Add theme switching function
    virtual void SetTheme(const std::string& theme_name) override;  // 切换主题
};

// RGB LCD显示器
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy,
                  DisplayFonts fonts);
};

// MIPI LCD显示器
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy,
                   DisplayFonts fonts);
};

// // SPI LCD显示器
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy,
                  DisplayFonts fonts);
};

// QSPI LCD显示器
class QspiLcdDisplay : public LcdDisplay {
public:
    QspiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy,
                   DisplayFonts fonts);
};

// MCU8080 LCD显示器
class Mcu8080LcdDisplay : public LcdDisplay {
public:
    Mcu8080LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                      int width, int height, int offset_x, int offset_y,
                      bool mirror_x, bool mirror_y, bool swap_xy,
                      DisplayFonts fonts);
};
#endif // LCD_DISPLAY_H
