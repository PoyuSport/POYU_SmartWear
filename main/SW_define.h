#pragma once
#include <cstddef>

////////////////////////////////////////// CONFIG //////////////////////////////////////////////
// 硬件配置
#define SCREEN_WIDTH                    128
#define SCREEN_HEIGHT                   64
// OLED引脚配置  VCC 5V
#define OLED_MOSI                       35 // D1
#define OLED_SCL                        36  // D0
#define OLED_DC                         47
#define OLED_CS                         45
#define OLED_RESET                      48

// MPU9250引脚配置  VCC ---> 3.3V  AD0 ---> GND
#define MPU9250_SDA                     8
#define MPU9250_SCL                     9

#define MPU9250_SDA_ULP                 1   // ULP RISC-V RTC I2C pin config , Only GPIO#1 or GPIO#3 can be used as the SDA pin.
#define MPU9250_SCL_ULP                 2   // ULP RISC-V RTC I2C pin config , Only GPIO#0 or GPIO#2 can be used as the SDA pin.

// MS5611引脚配置    VCC ---> 3.3V   CSB ---> GND
#define MS5611_SDA                      12
#define MS5611_SCL                      13

#define IMU_ADDR                        0x68
#define BARO_ADDR                       0x77
//#define LED_PIN 2

// MAX17043 电池电量传感器引脚配置
#define MAX17043_SDA                    10
#define MAX17043_SCL                    46

// RGB_LED
#define RGB_PIN                         42
#define NUM_LEDS                        1 // 板载LED数量

// TP4056充电
#define CE_PIN                          11 // TP4056的CE引脚，高电平使能,低电平禁止充电

// // EEPROM
// #define EEPROM_SIZE 64
// #define EEPROM_MAGIC 0xAA55
// #define EEPROM_VERSION 2

//////////////////////////////////// FLAG //////////////////////////////////////////////

// 推理标志位
#ifndef ENABLE_DEDUCTION_FLAG
#define ENABLE_DEDUCTION_FLAG           1 // 1: 模型推理    0: 模型数据采集
#endif

// TFLITE模型是否量化
#ifndef TFLITE_INT8_NORMALIZE_FLAG
#define TFLITE_INT8_NORMALIZE_FLAG      0 // 1: 归一化   0: 不归一化
#endif

#define TIME_INTERVAL                   20
#define SAMPLE_RATE_HZ                  50       // 明确采样率
#define WINDOW_BUFFER_SIZE              200

#define DEDUCE_INTERVAL                 2

// 推理数据条数
#define INFERENCE_DATA_NUM              50

#define TF_LITE_USE_PARAMETER_13        0 // 1: 使用13参数模型    0: 使用12参数模型
#if TF_LITE_USE_PARAMETER_13
        // 数据形状
        #define DATA_SHAPE              13 
#else
        #define DATA_SHAPE              12
#endif

// AI推理需要100个元素
#define AI_BUFFER_SIZE                    2*INFERENCE_DATA_NUM       

// IMU清除EEPROM校准数据标志位
#define ENABLE_IMU_CLEAR_CALIB_FLAG       0 // 1: 清除校准数据    0: 不清除

// MS5611循环设置基准周期
#define MS5611_CIRCLE_PERIOD              120        // 2min
// 气压计分辨率
#define BARO_RESOLUTION                   0.7

#define ENABLE_IMU_TYPE_MPU9250           0

#define ENABLE_ACCEL_FLAG                 1     // 1: 使能加速度计唤醒       0: 不使能
#define ENABLE_IMU_FLAG                   0     // 1: 使能陀螺仪唤醒         0: 不使能
#define ENABLE_SOC_ALERT_FLAG             1     // 1: 使能电池电量警报唤醒    0: 不使能

#define SOC_THRESHOLD                     3.0f     // 电池电量阈值

////////////////////////////////////////////////////DMA////////////////////////////////////////////////////

#ifndef DMA_SUPPORT
#define ALIGNMENT_SIZE 64
#if TFLITE_INT8_NORMALIZE_FLAG
const size_t buffer_block =
    ((INFERENCE_DATA_NUM * DATA_SHAPE * sizeof(int8_t) + ALIGNMENT_SIZE - 1) /
     ALIGNMENT_SIZE * ALIGNMENT_SIZE);
const size_t ai_buffer_block =
    ((AI_BUFFER_SIZE * DATA_SHAPE * sizeof(int8_t) + ALIGNMENT_SIZE - 1) /
     ALIGNMENT_SIZE * ALIGNMENT_SIZE);

#else
const size_t buffer_block = ((INFERENCE_DATA_NUM * DATA_SHAPE * sizeof(float)+ ALIGNMENT_SIZE - 1) /
     ALIGNMENT_SIZE * ALIGNMENT_SIZE);
const size_t ai_buffer_block = ((AI_BUFFER_SIZE * DATA_SHAPE * sizeof(float) + ALIGNMENT_SIZE - 1) /
     ALIGNMENT_SIZE * ALIGNMENT_SIZE);

#endif
#else
#define ALIGNMENT_SIZE 16
#if TFLITE_INT8_NORMALIZE_FLAG
        const size_t buffer_block = INFERENCE_DATA_NUM * DATA_SHAPE * sizeof(int8_t);
        const size_t ai_buffer_block = AI_BUFFER_SIZE * DATA_SHAPE * sizeof(int8_t);
#else
        const size_t buffer_block = INFERENCE_DATA_NUM * DATA_SHAPE * sizeof(float);
        const size_t ai_buffer_block = AI_BUFFER_SIZE * DATA_SHAPE * sizeof(float);
#endif 
#endif

#if TFLITE_INT8_NORMALIZE_FLAG
        const size_t rpm_buffer_block = WINDOW_BUFFER_SIZE * sizeof(int8_t);

#else
        const size_t rpm_buffer_block = WINDOW_BUFFER_SIZE * sizeof(float);
#endif 


#define DMA_SUPPORT 1

//////////////////////////////////// TEST //////////////////////////////////////////////

#define TEST_SHOW_FREE_STACK_FLAG 0                     // 1: 显示剩余栈空间    

#define TEST_SHOW_FREE_SPIRAM_FLAG 0                    // 1: 显示剩余SPIRAM空间

#define TEST_SHOW_ESP_GET_TIME_FLAG 0                   // 1: 显示ESP获取时间

#define TEST_SHOW_DEDUCE_TIME_FLAG 1                 // 1: 显示推理时间

// 使能低功耗蓝牙MAC地址显示
#define TEST_ENABLE_BLE_MAC_ADDR 0

#define TEST_SHOW_BLE_FREE_STACK_FLAG 0                 // 1: 显示剩余栈空间    
