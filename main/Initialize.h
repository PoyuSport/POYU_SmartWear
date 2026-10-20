#ifndef INIT_SENSOR_H
#define INIT_SENSOR_H

#include <RPM.h>
#include <BarometerDriver.h>
#include <SW_define.h>
#include <SW_struct.h>
#include <EEPROM_Utils.h>
#if ENABLE_IMU_TYPE_MPU9250
#include <MPU9250.h>
#else
#include <ICM_20948.h>
#endif
#include <MAX17043.h>
#include <PowerManager.h>
#include <BLE.h>
#include <BLEAdvertising.h>

#include <WiFi_AP.h>

#include <Wire.h>
#include <Softwire.h>
#include <driver/adc.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string.h>

#include <Preferences.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <FastLED.h>
#include <SC7A22H.h>

#include <esp_sleep.h>

#include "esp_heap_caps.h"
/**************************** */
#if ENABLE_DEDUCTION_FLAG
    #if TFLITE_INT8_NORMALIZE_FLAG
        #include "model_io_int8.h"
    #else
        #include "model_io12_float32.h"
    #endif
    #include <tensorflow/lite/c/common.h>
    #include <tensorflow/lite/micro/micro_interpreter.h>
    #include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
    #include <tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h>
    #include <tensorflow/lite/schema/schema_generated.h>
#endif
/**************************** */

extern "C" { // 必须包裹 C 语言头文件
#include "esp_log.h"
}

/// Preferences 存储  NVS
extern Preferences prefs;

// GRB-LED
extern CRGB leds[NUM_LEDS];

#if ENABLE_IMU_TYPE_MPU9250
extern MPU9250 imu;
#else
extern ICM_20948_I2C imu; 
#endif

extern BarometerDriver baro;

extern SFE_MAX1704X lipo; 

extern SensorState sensorState;

extern std::atomic<int> ledState;

extern std::atomic<bool> g_WiFiIsConnected;

extern SoftWire softWire;

#if ENABLE_DEDUCTION_FLAG
    extern std::unique_ptr<tflite::MicroInterpreter> interpreter;

    extern TfLiteTensor *input;
    extern TfLiteTensor *output;
#endif

#if TFLITE_INT8_NORMALIZE_FLAG
    // 双缓冲区定义（预分配）
    extern int8_t *buffer_ping;
    extern int8_t *buffer_pong;
    extern int8_t *aiBuffer_copy;

    // 全局定义量化参数
    static float input_scale = 0.0659235343337059f; // 输入张量的量化参数
    static int32_t input_zero_point = 0;            // 根据实际模型调整
    static float output_scale = 0.00390625;         // 输出张量的量化参数
    static int32_t output_zero_point = -128;
#else
    extern float *buffer_ping;
    extern float *buffer_pong;
    extern float *aiBuffer_copy;
#endif

#if ENABLE_DEDUCTION_FLAG
    #if TFLITE_INT8_NORMALIZE_FLAG
    // 在模型初始化时调用
    void load_quantization_params();
    #endif
#endif

void init_psram_buffers();
void scanI2C(TwoWire &i2cBus);
void scanSoftI2C(SoftWire &softI2CBus);

#if ENABLE_IMU_TYPE_MPU9250
// IMU校准
void calibrationIMU();
#endif

void init_external_32k_crystal();
void init_WiFiAP();
void init_IMU();
void init_MAX17043();
void init_MS5611();
void init_AI();
void init_RGBLED();
void init_BLE();
void init_Accel();

#endif