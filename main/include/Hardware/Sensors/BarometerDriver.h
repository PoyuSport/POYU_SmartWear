#pragma once
#include "MS5611.h"
#include <Wire.h>
#include <array>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "common_func.h"

struct BaroData {
    float pressure; // 气压 (hPa)
    float temperature; // 温度
    float altitude; // 海拔高度 (m)
};

class BarometerDriver {
  public:
    /**
     * @brief 气压计驱动构造函数
     * @param wire I2C总线实例
     * @param sda_pin SDA引脚
     * @param scl_pin SCL引脚
     * @param address 传感器地址 (默认0x77)
     */
    BarometerDriver(TwoWire &wire, uint8_t address = 0x77)
        : i2c_bus(wire), m_address(address), sea_level_hpa(1013.25),
          is_initialized(false) {
        baro = MS5611(address, &i2c_bus);
        baro.setOversampling(OSR_ULTRA_HIGH); // 设置采样率为最高
    }

    /**
     * @brief 初始化气压计
     * @return true 成功, false 失败
     */
    bool begin() {
        if (!baro.begin()) {
            return false;
        }

        is_initialized = true;
        return true;
    }

    BaroData readValue() {
        baro.read();
        BaroData bd;
        bd.temperature = sanitizeValue(baro.getTemperature());
        bd.pressure = sanitizeValue(baro.getPressure());
        return bd;
    }

    float getCompensatedHeight(float current_pressure_hPa) {
        float altitude = 44330 * (1 - pow(current_pressure_hPa / base_pressure, 1/5.255f));
        return altitude;
    }

    // 虚温（Virtual Temperature） 来综合补偿温度和湿度的影响
    float getAltitude(float current_pressure_hPa) {
        // 1. 计算虚温（使用当前气压）
        float T = base_temp + 273.15; // 转开尔文
        float sat_vapor = 6.112 * exp((17.67 * base_temp) / (base_temp + 243.5));
        float actual_vapor = sat_vapor * relative_humidity / 100.0;
        float Tv = T * (1 + 0.378 * actual_vapor / current_pressure_hPa); // 虚温
        
        // 2. 使用ISA公式计算海拔高度
        const float R = 287.05;   // 气体常数 (J/kg·K)
        const float g = 9.80665;  // 重力加速度 (m/s²)
        const float L = 0.0065;   // 温度递减率 (K/m)
        
        // 核心公式：h = (Tv/L) * [1 - (P/P0)^(R*L/g)]
        float altitude = (Tv / L) * (1 - pow(current_pressure_hPa / sea_level_hpa, (R * L) / g));
        
        return altitude;
    }

    float getSurfaceAltitude(float current_pressure_hPa, float reference_pressure_hPa) {
        // 1. 计算虚温（使用当前气压）
        float T = base_temp + 273.15; // 转开尔文
        float sat_vapor = 6.112 * exp((17.67 * base_temp) / (base_temp + 243.5));
        float actual_vapor = sat_vapor * relative_humidity / 100.0;
        float Tv = T * (1 + 0.378 * actual_vapor / current_pressure_hPa); // 虚温
        
        // 2. 使用ISA公式计算高度差（正确实现）
        const float R = 287.05;   // 气体常数 (J/kg·K)
        const float g = 9.80665;  // 重力加速度 (m/s²)
        const float L = 0.0065;   // 温度递减率 (K/m)
        
        // 核心公式：h = (Tv/L) * [1 - (P/P0)^(R*L/g)]
        float height = (Tv / L) * (1 - pow(current_pressure_hPa / reference_pressure_hPa, (R * L) / g));
        
        return height;
    }

    /**
     * @brief 进入低功耗模式
     */
    void sleep() {
        // BMP085不支持硬件休眠，通过降低采样率实现
        i2c_bus.endTransmission(true); // 释放I2C总线
    }

    /**
     * @brief 设置参考海平面气压
     * @param hpa 海平面气压值 (hPa)
     */
    void setSeaLevelPressure(float hpa) { sea_level_hpa = hpa; }

    float getSeaLevelPressure() { return sea_level_hpa; }

    float getBasePressure() const { return base_pressure; }

    /**
     * @brief 获取此时此地气压
     * @return 此时此地气压值 (hPa)
     */
    void setBasePressure(float hpa) { base_pressure = hpa; }

    /**
     * @brief 获取环境温度
     * @return 环境温度 (摄氏度)
     */
    void setBaseTemperature(float temperature) {  base_temp = temperature; }

    /**
     * @brief 设置相对湿度
     * @param humidity 相对湿度整数值 (0-100%)
     */
    void setHumidity(float humidity) { relative_humidity = humidity; }

    /**
     * @brief 获取初始化状态
     */
    bool isInitialized() const { return is_initialized; }

  private:
    MS5611 baro;
    TwoWire &i2c_bus;
    uint8_t m_address;
    float sea_level_hpa;    // 海平面参考气压
    float base_temp = 25.0; // 基准温度
    float base_pressure = 1013.25;
    float relative_humidity = 60.0;
    bool is_initialized;
};
