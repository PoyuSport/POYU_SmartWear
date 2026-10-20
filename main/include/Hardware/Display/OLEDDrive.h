#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

class OLEDDriver {
  public:
    /**
     * @brief 构造函数（SPI 模式）
     * @param screen_width 屏幕宽度
     * @param screen_height 屏幕高度
     * @param MOSI SPI MOSI 引脚
     * @param SCL SPI SCLK 引脚
     * @param DC   数据/命令选择引脚
     * @param RESET   片选引脚
     * @param CS  复位引脚
     */
    OLEDDriver(uint8_t screen_width, uint8_t screen_height, uint8_t MOSI,
               uint8_t SCL, uint8_t DC, uint8_t RESET, uint8_t CS)
        : is_initialized(false), power_state(true) {
        display = Adafruit_SSD1306(screen_width, screen_height, MOSI, SCL, DC,
                                   RESET, CS);
    }

    /**
     * @brief 初始化屏幕
     * @return true 成功, false 失败
     */
    bool begin() {

        if (!display.begin(SSD1306_SWITCHCAPVCC)) {
            return false;
        }

        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.cp437(true); // 启用 Code Page 437 字符集

        is_initialized = true;
        return true;
    }

    /**
     * @brief 更新显示（必须调用才会刷新屏幕）
     */
    void updateDisplay() {
        if (!is_initialized || !power_state)
            return;
        display.display();
    }

    /**
     * @brief 清空显示缓冲区
     */
    void clear() {
        if (!is_initialized)
            return;
        display.clearDisplay();
    }

    /**
     * @brief 绘制文本
     * @param x 起始X坐标
     * @param y 起始Y坐标
     * @param text 要显示的文本
     * @param size 字体大小 (1-4)
     */
    void drawText(uint8_t x, uint8_t y, const char *text, uint8_t size = 1) {
        if (!is_initialized)
            return;
        display.setTextSize(size);
        display.setCursor(x, y);
        display.print(text);
    }

    /**
     * @brief 绘制线段
     * @param x0 起点X
     * @param y0 起点Y
     * @param x1 终点X
     * @param y1 终点Y
     */
    void drawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
        if (!is_initialized)
            return;
        display.drawLine(x0, y0, x1, y1, SSD1306_WHITE);
    }

    /**
     * @brief 关闭屏幕（低功耗模式）
     */
    void turnOff() {
        if (!is_initialized)
            return;
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        power_state = false;
    }

    /**
     * @brief 开启屏幕
     */
    void turnOn() {
        if (!is_initialized)
            return;
        display.ssd1306_command(SSD1306_DISPLAYON);
        power_state = true;
    }

    /**
     * @brief 获取屏幕当前状态
     * @return true 屏幕开启, false 关闭
     */
    bool getPowerState() const { return power_state; }

  private:
    Adafruit_SSD1306 display;
    bool is_initialized; // 初始化状态标志
    bool power_state;    // 电源状态
};