#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <string.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "esp_heap_caps.h"

#include <ArduinoEigen.h>  // 安装ArduinoEigen库

// 系统状态
enum SensorState { INIT, CALIBRATING, RUNNING, ERROR };

enum LEDState { 
                eOFF = 0, 
                eBLUE = 1,
                eGREEN = 2, 
                eORANGE = 3,
                eRED = 4,
                ePURPLE = 5,
                eGRAY = 6,
                eYELLOW = 7,
                eWHITE = 8
            }; 

// 定义浮点类型为float以节省资源
using Matrix3f = Eigen::Matrix<float, 3, 3>;
using Vector3f = Eigen::Matrix<float, 3, 1>;
using Quaternionf = Eigen::Quaternion<float>;

struct ImuRawData{
    Vector3f accel;
    Vector3f gyro;
    Quaternionf quat;
    Vector3f line_accel;
};

struct IMUData {
    float pitch;
    float yaw;
    float roll;
    float temperature;
    ImuRawData rawData; // 原始数据
};

struct __attribute__((packed, aligned(4))) FusionPacket {
    float roll;
    float pitch;
    float yaw;

    float accel_x;          // 加速度
    float accel_y;
    float accel_z;

    float gyro_x;           // 陀螺仪
    float gyro_y;   
    float gyro_z;

    float vert_line_accel;    // 去除重力作用的加速度分量

    float accel_mag;          // 加速度模长
    float hor_accel;          // 水平方向加速度强度
    float vert_percent;       // 垂直占比

    float quat_x;           // 四元数   
    float quat_y;
    float quat_z;  
    float quat_w;

    float rpm;
    int     type;               // 运动类型
    uint32_t timestamp;
};
static_assert(sizeof(FusionPacket) == (72+8), "FusionPacket size mismatch");
static_assert(offsetof(FusionPacket, type) == 72, "Type offset error");
static_assert(offsetof(FusionPacket, timestamp) == 76, "Alignment error");

#define BATCH_SIZE 8
struct __attribute__((packed)) FusionBatch {
    FusionPacket packets[BATCH_SIZE];
    uint32_t timestamp; // 批次时间戳
    uint16_t sequence;  // 批次序列号
    uint8_t padding[2];     // 新增2字节填充，使大小为4的倍数
};
// 更新断言
static_assert(sizeof(FusionBatch) == 648, "Batch size mismatch");  // 646+2=648
static_assert(offsetof(FusionBatch, timestamp) == 640, "时间戳偏移错误");
static_assert(offsetof(FusionBatch, sequence) == 644, "序列号偏移错误");
static_assert(offsetof(FusionBatch, padding) == 646, "填充偏移错误");

#define BUFFER_DEPTH 4 // 可存储4个完整批次
struct {
    FusionBatch batches[BUFFER_DEPTH];
    volatile uint8_t wr_ptr = 0;
    volatile uint8_t rd_ptr = 0;
    volatile uint16_t sequence = 0;
    SemaphoreHandle_t mutex;
} batchBuffer;

// 运动类型
enum SportType {
    NOKNOW = 0,     // 未知运动
    WALKING = 1,      // 步行
    BYCLING = 2,      // 骑行
    JOGGING = 3,      // 慢跑
};


struct AltitudeInfo{
    float altitude;  // 海拔高度
    float cilmbhigh; // 爬升高度
};

struct DataPacket {
    uint32_t timestamp; // 时间戳
    uint8_t type;       // 运动类型
    uint16_t duration;  // 运动持续时间
    uint8_t battery;    // 电池电量
    uint32_t altitude;  // 海拔高度
    uint32_t ascentHeight;  // 爬升高度
    uint32_t rpm;          // 转速
    uint32_t sportLaps; // 运动圈数
    float prob;         // 概率
};