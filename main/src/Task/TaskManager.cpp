#include <TaskManager.h>

/////////////////////////// Hardware
#include <BLE.h>
#include <BLEAdvertising.h>
#include <BarometerDriver.h>
#include <EEPROM_Utils.h>
#include <MAX17043.h>
#if ENABLE_IMU_TYPE_MPU9250
#include <MPU9250.h>
#else
#include <ICM_20948.h>
#include <ICM_20948_DMP.h>
#endif

//////////////////////////  Power
#include <PowerManager.h>

///////////////////////// Algorithm

#include <ButterworthPointFilter.h>
#include <RPM.h>

///////////////////////////   AI
#include <DLFeatureSet.h>

////////////////////////// SDCard
#include <SDCard.h>

/////////////////////////// define
#include <SW_define.h>
#include <SW_struct.h>
#include <common_func.h>

// #include <WiFi_AP.h>

// #include <driver/adc.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string.h>

#include <Preferences.h>

/////////////////////////// FreeRTOS
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

/////////////////////////// Socket
#include <AsyncWebSocket.h>

/////////////////////////// RGBLED
#include <FastLED.h>

#include "esp_heap_caps.h"
#include <esp_sleep.h>

extern "C" { // 必须包裹 C 语言头文件
#include "esp_log.h"
}

/////////////////////////// TensorFlow Lite

#include <tensorflow/lite/c/common.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h>
#include <tensorflow/lite/schema/schema_generated.h>

/////////////////////////// Eigen
#include <ArduinoEigen.h>
#include <ArduinoEigenDense.h>
#include <ArduinoEigenSparse.h> // 用于矩阵求逆


#include "canonical_protocol.h"
#include "BLEFrame.h"

using namespace CanonicalProtocol;

Encoder encoder;

bool active_buffer = false; // false=ping, true=pong
bool ai_active_buffer = false;

/// 滤波
static ButterworthPointFilter butterFilter;

// 在全局区域定义
std::atomic<TickType_t> last_ws_activity_time(0); // 最后一次WebSocket发送时间

// 开始推理 | true 表示运动开始;false 表示运动结束
extern std::atomic<bool> BLE::g_isStartDeduction;
// 开始运动计数
extern std::atomic<float> BLE::g_sportCount;
// 室外温度
extern std::atomic<float> BLE::g_temperature;
// 海平面气压
extern std::atomic<float> BLE::g_seaLevelPressure; // 标准大气压
// 表面大气压
extern std::atomic<float> g_surfacePressure; // 表面大气压
// 湿度
extern std::atomic<float> BLE::g_humidity;
// 进入低功耗模式/正常模式
extern std::atomic<bool> BLE::g_manualLowPowerMode;
// BLE状态信息
extern std::atomic<BLE::BLE_State> BLE::bleState;

extern std::atomic<bool> BLE::deviceConnected;

extern std::atomic<bool> active_mode;

// 外部是否更新了气压信息
extern std::atomic<bool> BLE::g_bUpdate;

extern std::atomic<int> ledState;

// WiFi连接状态
std::atomic<bool> g_WiFiIsConnected(false);

// 推理结束后更新运动类型
std::atomic<int> g_inferenceType(0);
// 推理结束后更新运动类型的概率
std::atomic<float> g_inferenceProb(0.0f);

extern float *pressBuffer;

/// Preferences 存储  NVS
extern Preferences prefs;

// GRB-LED
extern CRGB leds[NUM_LEDS];

extern int file_counter;

#if ENABLE_IMU_TYPE_MPU9250
extern MPU9250 imu;
#else
extern ICM_20948_I2C imu; 
#endif

extern BarometerDriver baro;

extern SFE_MAX1704X lipo;

extern SensorState sensorState;

extern AsyncWebSocket ws;

// 上一次从网络中获取的温度
static float lastTemperature = BLE::g_temperature;
// 上一次从网络中获取的湿度
static float lastHumidity = BLE::g_humidity;
// 上一次从网络中获取的气压值
static float lastPressure = BLE::g_surfacePressure;

std::atomic<float> baroTemperature(0.0f); // 气压计温度

#if ENABLE_DEDUCTION_FLAG
extern std::unique_ptr<tflite::MicroInterpreter> interpreter;

extern TfLiteTensor *input;
extern TfLiteTensor *output;
#endif

#if TFLITE_INT8_NORMALIZE_FLAG
// 双缓冲区定义（预分配）
extern int8_t *buffer_ping;
extern int8_t *buffer_pong;
extern int8_t *ai_buffer_ping;
extern int8_t *ai_buffer_pong;

extern int8_t *aiBuffer_copy;
#else
extern float *buffer_ping;
extern float *buffer_pong;
extern float *ai_buffer_ping;
extern float *ai_buffer_pong;

extern float *aiBuffer_copy;
#endif

// 爬升高度
std::atomic<float> g_climbHeight(0.0f);
// 长时间大气气压波动影响的总的高度变化值
std::atomic<float> deltaHeight_Sum(0.0f);
std::atomic<double> battery_voltage(0.0f);
std::atomic<double> battery_soc(0.0f);
std::atomic<int16_t> buffer_index(0);

RPM::RPM_Compute pitch_rpm;
RPM::RPM_Compute roll_rpm;

namespace TaskManager {

/******************** FreeRTOS*********************** */
// 互斥锁（保护共享资源）
SemaphoreHandle_t fusion_mutex = xSemaphoreCreateMutex();
// 操作互斥锁
SemaphoreHandle_t bufferMutex = xSemaphoreCreateMutex(); // 缓冲区互斥
// 推理互斥锁
SemaphoreHandle_t tflite_mutex = xSemaphoreCreateMutex();
SemaphoreHandle_t fastLED_mutex = xSemaphoreCreateMutex();

// RPM互斥锁
SemaphoreHandle_t rpm_mutex = xSemaphoreCreateMutex();
SemaphoreHandle_t imuMutex = xSemaphoreCreateMutex();  // IMU数据保护
SemaphoreHandle_t baroMutex = xSemaphoreCreateMutex(); // 气压计数据保护
SemaphoreHandle_t wsMutex = xSemaphoreCreateMutex();   // WebSocket

// 队列（任务间通信）
QueueHandle_t baroDataQueue =
    xQueueCreate(5, sizeof(AltitudeInfo)); // 爬升高度信息
// 队列（任务间通信）
QueueHandle_t imuDataQueue = xQueueCreate(5, sizeof(IMUData)); // IMU数据传输
QueueHandle_t wifiDataQueue = xQueueCreate(50, sizeof(FusionPacket[8]));
QueueHandle_t bleDataQueue = xQueueCreate(10, sizeof(std::string *));

#if TFLITE_INT8_NORMALIZE_FLAG
QueueHandle_t aiInferQueue =
    xQueueCreate(5, sizeof(int8_t *)); // AI存储INFERENCE_DATA_NUM帧姿态角数据

QueueHandle_t rpmQueue =
    xQueueCreate(5, sizeof(int8_t *)); // RPM存储INFERENCE_DATA_NUM帧姿态角数据
#else
QueueHandle_t aiInferQueue =
    xQueueCreate(5, sizeof(float *)); // 存储INFERENCE_DATA_NUM帧姿态角数据

QueueHandle_t rpmQueue =
    xQueueCreate(5, sizeof(float *)); // RPM存储INFERENCE_DATA_NUM帧姿态角数据
#endif

// 新增全局变量
TaskHandle_t taskFusionHandle = NULL;
TaskHandle_t taskBaroHandle = NULL;
TaskHandle_t taskIMUHandle = NULL;
TaskHandle_t taskBLEHandle = NULL;
TaskHandle_t taskAIHandle = NULL;
TaskHandle_t taskBatteryHandle = NULL;
// 声明全局任务句柄
TaskHandle_t taskStorageHandle = NULL;
TaskHandle_t taskComputeTrackerHandle = NULL; // 计算身体追踪器
TaskHandle_t taskRPMHandle = NULL;
TaskHandle_t taskFastLEDHandle = NULL;

void safe_free(void *ptr, const char *tag) {
    if (ptr == nullptr) {
        ets_printf("Attempt to free null pointer [%s]", tag);
        return;
    }

    // 1. 验证指针是否属于SPIRAM
    if (!esp_ptr_external_ram(ptr)) {
        ets_printf("Pointer %p not in SPIRAM [%s]", ptr, tag);
        return;
    }

    // 2. 获取分配大小
    size_t size = heap_caps_get_allocated_size(ptr);
    ets_printf("Freeing %p (%u bytes) [%s]", ptr, (unsigned)size, tag);

    // 3. 在释放前检查堆完整性
    if (!heap_caps_check_integrity(MALLOC_CAP_SPIRAM, true)) {
        ets_printf("Heap corruption BEFORE free %p [%s]", ptr, tag);
        heap_caps_dump(MALLOC_CAP_SPIRAM);
    }

    // 4. 实际释放 - 使用SPIRAM专用API
    heap_caps_free(ptr);

    // 5. 释放后检查
    if (!heap_caps_check_integrity(MALLOC_CAP_SPIRAM, true)) {
        ets_printf("Heap corruption AFTER free %p [%s]", ptr, tag);
        heap_caps_dump(MALLOC_CAP_SPIRAM);
    }
    ptr = nullptr;
}

// 数据发送配置结构体
struct BLEStringBuilderConfig {
    const std::string &baseString;    // 基础字符串
    float maxProb;                    // 概率值（不修改原始值）
    float rpm;                        // 转速值
    const AltitudeInfo &altitudeInfo; // 海拔信息
    bool condition;                   // 条件标志
};

//  构建BLE数据字符串
//  NumericType 数值类型模板（支持int, float等）
//  config 配置参数
//  构建完成的字符串
template <typename NumericType>
std::string buildBLEString(const BLEStringBuilderConfig &config) {
    std::ostringstream oss;

    // 处理概率部分
    oss << config.baseString << ",stateProb:";
    oss << (config.maxProb * 100); // 转换为百分比

    // 处理RPM部分
    oss << ",RPM:";
    if (config.condition) {
        oss << static_cast<int>(config.rpm);
    } else {
        oss << "0";
    }

    // 添加固定字段
    oss << ",sportLaps:" << BLE::g_sportCount.load()
        << ",Alt:" << config.altitudeInfo.cilmbhigh
        << ",Altitude:" << config.altitudeInfo.altitude;

    return oss.str();
}

// 发送数据到队列（带内存管理）
// data 要发送的数据字符串
// 发送是否成功
bool sendToQueue(std::string &&data) {
    // 使用unique_ptr确保异常安全
    auto strPtr =
        std::unique_ptr<std::string>(new std::string(std::move(data)));
    printf("[BLE] strPtr size : %d", strPtr->size());
    if (xQueueSend(bleDataQueue, &strPtr, 0) == pdTRUE) {
        strPtr.release(); // 转移所有权到队列
        return true;
    }
    return false;
}

bool sendFrameToQueue(const uint8_t* data, size_t len) {
    if (!data || len == 0) return false;

    // 深拷贝一份，队列拥有所有权
    uint8_t* buf = new uint8_t[len];
    memcpy(buf, data, len);

    BLEFrame* frame = new BLEFrame(buf, len);

    if (xQueueSend(bleDataQueue, &frame, 0) == pdTRUE) {
        return true;
    }

    // 入队失败，释放
    delete frame;
    return false;
}

// 高级抽象：构建并发送BLE数据
// config 配置参数
void buildAndSendBLEString(const BLEStringBuilderConfig &config) {
    // 根据条件选择数值类型处理（int或float）
    auto bleString = config.condition ? buildBLEString<float>(config)
                                      : buildBLEString<int>(config);

    sendToQueue(std::move(bleString));
}

 std::vector<uint8_t> sendTelemetry(const DataPacket& dp) {
    PacketData pkt;
    pkt.setTimestamp(dp.timestamp);
    pkt.setMotionType(MotionType(dp.type), dp.prob*100);
    pkt.setMotionDuration(dp.duration);
    pkt.setAscentHeight(dp.ascentHeight); 
    pkt.setBattery(dp.battery);
    pkt.setAltitude(dp.altitude); 
    pkt.setRPM(dp.rpm);
    pkt.setSportLaps(dp.sportLaps);

    pkt.prepareTelemetryPacket();

    std::vector<uint8_t> frame = encoder.encodePacket(pkt);

    return frame;
}


// BLE通信任务（低优先级）
void taskBLEComms(void *pvParam) {
    static uint32_t last_Wake_Time = xTaskGetTickCount();
    static uint32_t current_Wake_Time = xTaskGetTickCount();

    while (1) {
        // 状态机控制（原子读取状态）
        BLE::BLE_State currentState = BLE::bleState.load();
        switch (currentState) {
        case BLE::BLE_IDLE: {
            // 空闲状态时每5秒检查一次唤醒条件
            static TickType_t lastWakeTime = xTaskGetTickCount();
            vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(5000));
            break;
        }

        case BLE::BLE_ADVERTISING: {
            // 广播状态下定期刷新广播数据
            static uint32_t advCounter = 0;
            if ((advCounter++ % 60) == 0) { // 每分钟刷新
                BLEAdvertisementData advData;
                advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC);
                advData.setName("POYUSPORT");
                BLEDevice::getAdvertising()->setAdvertisementData(advData);
            }
            vTaskDelay(pdMS_TO_TICKS(1000)); // 1秒周期
            break;
        }

        case BLE::BLE_CONNECTED: {
            BLEFrame* frame = nullptr;
            UBaseType_t uxMessagesWaiting =
                uxQueueMessagesWaiting(bleDataQueue);

            const TickType_t xMaxBlockTime =
                uxMessagesWaiting > 10 ? pdMS_TO_TICKS(1) : pdMS_TO_TICKS(50);

            if (xQueueReceive(bleDataQueue, &frame, xMaxBlockTime) == pdPASS) {
                BLE::safeSendFrame(frame->data, frame->len);
                delete frame; // 自动释放 data
            }

            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        }

        default:
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }

#if TEST_SHOW_FREE_STACK_FLAG
        UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
        printf("[BLE] Task Stack Free: %d\n",
                      stack * sizeof(StackType_t));
#endif
    }
}

// IMU数据采集任务（高优先级）
void taskIMUData(void *pvParam) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10); // 严格10ms周期
    while (1) {
        if (!PowerManager::is_active()) { // 低功耗模式时挂起任务
            vTaskSuspend(NULL);
            continue;
        }

        if (sensorState != RUNNING) { // 运行状态
            vTaskSuspend(NULL);       // 挂起自身，由状态管理器恢复
            continue;
        }

        // 读取IMU数据（带互斥锁保护）
        IMUData imuData;
        ImuRawData imuRawData;
        if (xSemaphoreTake(imuMutex, pdMS_TO_TICKS(1)) == pdTRUE) {

#if ENABLE_IMU_TYPE_MPU9250

            imu.update();

            imuData.yaw = sanitizeValue(imu.getYaw());
            imuData.roll = sanitizeValue(imu.getRoll());
            imuData.pitch = sanitizeValue(imu.getPitch());
            imuData.temperature = sanitizeValue(imu.getTemperature());

            // 东北天方向加速度
            Vector3f accel(-imu.getAccX(), imu.getAccY(), imu.getAccZ());

            // 东北天方向角速度
            Vector3f gyro(imu.getGyroX(), -imu.getGyroY(), -imu.getGyroZ());

            Quaternionf quat(imu.getQuaternionW(),imu.getQuaternionX(), imu.getQuaternionY(),
                             imu.getQuaternionZ());

            // 垂直线性加速度
            Vector3f line_accel(imu.getLinearAccX(),imu.getLinearAccY(),imu.getLinearAccZ());

            imuRawData.accel = accel;
            imuRawData.gyro = gyro; // 保存陀螺仪数据
            imuRawData.quat = quat; // 保存四元数数据
            imuRawData.line_accel = line_accel;
            imuData.rawData = imuRawData; // 保存原始数据
#else
            icm_20948_DMP_data_t data;
            imu.readDMPdataFromFIFO(&data);

            if ((imu.status == ICM_20948_Stat_Ok) || (imu.status == ICM_20948_Stat_FIFOMoreDataAvail)) // Was valid data available?
            {
                double q1 , q2 , q3;
                float acc_x , acc_y , acc_z;
                float gyro_x , gyro_y , gyro_z;
                double roll, pitch, yaw;
                double qw , qx , qy ,qz;
                acc_x = acc_y = acc_z = 0.0;
                gyro_x = gyro_y = gyro_z = 0.0;
                roll = pitch = yaw = 0.0;
                qw=qx=qy=qz=0.0;
                if ((data.header & DMP_header_bitmap_Quat6) > 0) // Check for GRV data (Quat6)
                {
                    q1 = sanitizeValue(data.Quat6.Data.Q1 / 1073741824.0); // Convert to double. Divide by 2^30
                    q2 = sanitizeValue(data.Quat6.Data.Q2/ 1073741824.0); // Convert to double. Divide by 2^30
                    q3 = sanitizeValue(data.Quat6.Data.Q3 / 1073741824.0); // Convert to double. Divide by 2^30
                    double q0 = sqrt(1.0 - ((q1 * q1) + (q2 * q2) + (q3 * q3)));

                    qw = q0; // See issue #145 - thank you @Gord1
                    qx = q2;
                    qy = q1;
                    qz = -q3;

                    // roll (x-axis rotation)
                    double t0 = +2.0 * (qw * qx + qy * qz);
                    double t1 = +1.0 - 2.0 * (qx * qx + qy * qy);
                    roll = atan2(t0, t1) * 180.0 / PI;

                    // pitch (y-axis rotation)
                    double t2 = +2.0 * (qw * qy - qx * qz);
                    t2 = t2 > 1.0 ? 1.0 : t2;
                    t2 = t2 < -1.0 ? -1.0 : t2;
                    pitch = asin(t2) * 180.0 / PI;

                    // yaw (z-axis rotation)
                    double t3 = +2.0 * (qw * qz + qx * qy);
                    double t4 = +1.0 - 2.0 * (qy * qy + qz * qz);
                    yaw = atan2(t3, t4) * 180.0 / PI;
                }

                if ((data.header & DMP_header_bitmap_Accel) > 0) // Check for Accel
                {
                    acc_x = sanitizeValue((data.Raw_Accel.Data.X/2048.0)); // Extract the raw accelerometer data
                    acc_y = sanitizeValue((data.Raw_Accel.Data.Y/2048.0));
                    acc_z = sanitizeValue((data.Raw_Accel.Data.Z/2048.0));
                }
                if((data.header & DMP_header_bitmap_Gyro) > 0) // Check for Gyro
                {
                    gyro_x = sanitizeValue(data.Raw_Gyro.Data.X)* 2000.0 / 32768.0; // Extract the raw gyroscope data
                    gyro_y = sanitizeValue(data.Raw_Gyro.Data.Y)* 2000.0 / 32768.0;
                    gyro_z = sanitizeValue(data.Raw_Gyro.Data.Z)*  2000.0 / 32768.0;
                }

                Vector3f accel(acc_x, acc_y, acc_z);
                Vector3f gyro(gyro_x, gyro_y, gyro_z);

                Quaternionf quat(qw,qx, qy,qz);

                // 计算线性加速度（去除重力）
                // 1. 定义世界坐标系（NED）中的重力向量
                Vector3f gravity_world(0.0f, 0.0f, 1.0f); // NED坐标系中Z轴向下

                // 2. 使用四元数将重力向量旋转到传感器坐标系
                Vector3f gravity_sensor = quat * gravity_world;

                // 3. 从原始加速度中减去重力分量
                Vector3f line_accel = accel - gravity_sensor;

                imuData.yaw = sanitizeValue(yaw);
                imuData.roll = sanitizeValue(roll);
                imuData.pitch = sanitizeValue(pitch);
                imuData.temperature = 0;

                imuRawData.accel = accel;
                imuRawData.gyro = gyro; // 保存陀螺仪数据
                imuRawData.quat = quat; // 保存四元数数据
                imuRawData.line_accel = line_accel;
                imuData.rawData = imuRawData; // 保存原始数据
            }
#endif
            xSemaphoreGive(imuMutex);
        }


        // 发送到队列（非阻塞）
        xQueueSend(imuDataQueue, &imuData, pdMS_TO_TICKS(2));
        vTaskDelayUntil(&xLastWakeTime, xFrequency);


#if TEST_SHOW_FREE_STACK_FLAG
        UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
        printf("[Sensor Read] Task Stack Free: %d\n",
                      stack * sizeof(StackType_t));
#endif
    }
}

// 数据融合与姿态解算任务
void taskDataFusion(void *pvParam) {
    printf("---------------------------------- taskDataFusion --------------");
    const TickType_t xFrequency = pdMS_TO_TICKS(20); // 20ms采样间隔
    TickType_t xLastWakeTime = xTaskGetTickCount();

    static int64_t last_fusion_time = 0; // 记录上一次包发送时间

    // 类似定时器，定时进行推理
    static int deduce_interval = DEDUCE_INTERVAL;

#if !ENABLE_DEDUCTION_FLAG
    static int64_t last_packet_time = 0; // 记录上一次WiFi数据包发送时间
    static float rpm = 0.0;              // RPM
#endif

    while (1) {
        if (!PowerManager::is_active()) { // 低功耗模式时挂起任务
            vTaskSuspend(NULL);
            continue;
        }

        if (sensorState != RUNNING) { // 运行状态
            vTaskSuspend(NULL);       // 挂起自身，由状态管理器恢复
            continue;
        }


        // 读取IMU数据（带互斥锁保护）
        IMUData imuData;
        DLFeatureSet::FeatureSet imuFeatureSet;
        DLFeatureSet::Feature imuFeature;
        Vector3f ENUAccel;
        Vector3f ENUGyro;
        if (xQueueReceive(imuDataQueue, &imuData, pdMS_TO_TICKS(2)) == pdTRUE) {
            imuFeature.setPitchRoll(sanitizeValue(imuData.pitch), sanitizeValue(imuData.roll));
            imuFeature.setAccelGyro(
                Vector3f(sanitizeValue(imuData.rawData.accel.x()), sanitizeValue(imuData.rawData.accel.y()),
                         sanitizeValue(imuData.rawData.accel.z())),
                Vector3f(sanitizeValue(imuData.rawData.gyro.x()), sanitizeValue(imuData.rawData.gyro.y()),
                         sanitizeValue(imuData.rawData.gyro.z())));
            imuFeature.setLineAccel(imuData.rawData.line_accel);
            ENUAccel = imuFeature.getENUAccel();
            ENUGyro = imuFeature.getENUGyro();

            imuFeatureSet.accel_x = sanitizeValue(ENUAccel.x());
            imuFeatureSet.accel_y = sanitizeValue(ENUAccel.y());
            imuFeatureSet.accel_z = sanitizeValue(ENUAccel.z());

            imuFeatureSet.gyro_x = sanitizeValue(ENUGyro.x());
            imuFeatureSet.gyro_y = sanitizeValue(ENUGyro.y());
            imuFeatureSet.gyro_z = sanitizeValue(ENUGyro.z());

            imuFeatureSet.vert_line_accel = imuFeature.getVerticalLineAccel();

            imuFeatureSet.accel_mag = imuFeature.getAccelMagnitude();
            imuFeatureSet.hor_accel = imuFeature.getHorizontalAccel();
            imuFeatureSet.vert_percent = sanitizeValue(imuFeature.getVerticalPercent());

            imuFeatureSet.roll = sanitizeValue(imuData.roll);
            imuFeatureSet.pitch = sanitizeValue(imuData.pitch);
            imuFeatureSet.yaw = sanitizeValue(imuData.yaw);

#if TFLITE_INT8_NORMALIZE_FLAG
            int8_t *target = nullptr;
#else
            float *target = nullptr;
#endif

            static float rpm = 0.0f;
            if (xSemaphoreTake(fusion_mutex, pdMS_TO_TICKS(10))) {  
                //   更新数据缓冲区
                target = active_buffer ? buffer_ping : buffer_pong;


#if TF_LITE_USE_PARAMETER_13
    #if TFLITE_INT8_NORMALIZE_FLAG
                float normalized_roll_value  = imuFeatureSet.roll / 180.0f;
                float normalized_pitch_value = imuFeatureSet.pitch / 90.0f;
                float normalized_yaw_value   = imuFeatureSet.yaw / 180.0f;

                float normalized_accel_x = imuFeatureSet.accel_x / 16.0f;
                float normalized_accel_y = imuFeatureSet.accel_y / 16.0f;
                float normalized_accel_z = imuFeatureSet.accel_z / 16.0f;

                //  gyro ±2000 dps
                float normalized_gyro_x = imuFeatureSet.gyro_x / 2000.0f;
                float normalized_gyro_y = imuFeatureSet.gyro_y / 2000.0f;
                float normalized_gyro_z = imuFeatureSet.gyro_z / 2000.0f;

                target[buffer_index * DATA_SHAPE + 0] =
                    QuantizeToInt8(normalized_roll_value, input_scale, input_zero_point);
                target[buffer_index * DATA_SHAPE + 1] =
                    QuantizeToInt8(normalized_pitch_value, input_scale, input_zero_point);
                target[buffer_index * DATA_SHAPE + 2] =
                    QuantizeToInt8(normalized_yaw_value, input_scale, input_zero_point);

                target[buffer_index * DATA_SHAPE + 3] =
                    QuantizeToInt8(normalized_accel_x, input_scale, input_zero_point);
                target[buffer_index * DATA_SHAPE + 4] =
                    QuantizeToInt8(normalized_accel_y, input_scale, input_zero_point);
                target[buffer_index * DATA_SHAPE + 5] =
                    QuantizeToInt8(normalized_accel_z, input_scale, input_zero_point);

                // gyro 归一化后再量化
                target[buffer_index * DATA_SHAPE + 6] =
                    QuantizeToInt8(normalized_gyro_x, input_scale, input_zero_point);
                target[buffer_index * DATA_SHAPE + 7] =
                    QuantizeToInt8(normalized_gyro_y, input_scale, input_zero_point);
                target[buffer_index * DATA_SHAPE + 8] =
                    QuantizeToInt8(normalized_gyro_z, input_scale, input_zero_point);

                target[buffer_index * DATA_SHAPE + 9] =
                    QuantizeToInt8(imuFeatureSet.vert_line_accel, input_scale, input_zero_point);

                target[buffer_index * DATA_SHAPE + 10] =
                    QuantizeToInt8(imuFeatureSet.accel_mag, input_scale, input_zero_point);

                target[buffer_index * DATA_SHAPE + 11] =
                    QuantizeToInt8(imuFeatureSet.hor_accel, input_scale, input_zero_point);

                target[buffer_index * DATA_SHAPE + 12] =
                    QuantizeToInt8(imuFeatureSet.vert_percent, input_scale, input_zero_point);

#else

                /// 归一化后
                target[buffer_index * DATA_SHAPE + 0] = imuFeatureSet.roll;
                target[buffer_index * DATA_SHAPE + 1] = imuFeatureSet.pitch;
                target[buffer_index * DATA_SHAPE + 2] = imuFeatureSet.yaw;

                target[buffer_index * DATA_SHAPE + 3] = imuFeatureSet.accel_x;
                target[buffer_index * DATA_SHAPE + 4] = imuFeatureSet.accel_y;
                target[buffer_index * DATA_SHAPE + 5] = imuFeatureSet.accel_z;

                target[buffer_index * DATA_SHAPE + 6] = imuFeatureSet.gyro_x;
                target[buffer_index * DATA_SHAPE + 7] = imuFeatureSet.gyro_y;
                target[buffer_index * DATA_SHAPE + 8] = imuFeatureSet.gyro_z;

                target[buffer_index * DATA_SHAPE + 9] =
                    imuFeatureSet.vert_line_accel;

                target[buffer_index * DATA_SHAPE + 10] =
                    imuFeatureSet.accel_mag;
                target[buffer_index * DATA_SHAPE + 11] =
                    imuFeatureSet.hor_accel;
                target[buffer_index * DATA_SHAPE + 12] =
                    imuFeatureSet.vert_percent;

    #endif
#else
    #if TFLITE_INT8_NORMALIZE_FLAG
                float normalized_roll_value  = imuFeatureSet.roll / 180.0f;
                float normalized_pitch_value = imuFeatureSet.pitch / 90.0f;

                float normalized_accel_x = imuFeatureSet.accel_x / 16.0f;
                float normalized_accel_y = imuFeatureSet.accel_y / 16.0f;
                float normalized_accel_z = imuFeatureSet.accel_z / 16.0f;

                //  gyro ±2000 dps
                float normalized_gyro_x = imuFeatureSet.gyro_x / 2000.0f;
                float normalized_gyro_y = imuFeatureSet.gyro_y / 2000.0f;
                float normalized_gyro_z = imuFeatureSet.gyro_z / 2000.0f;

                target[buffer_index * DATA_SHAPE + 0] =
                    QuantizeToInt8(normalized_roll_value, input_scale, input_zero_point);
                target[buffer_index * DATA_SHAPE + 1] =
                    QuantizeToInt8(normalized_pitch_value, input_scale, input_zero_point);

                target[buffer_index * DATA_SHAPE + 2] =
                    QuantizeToInt8(normalized_accel_x, input_scale, input_zero_point);
                target[buffer_index * DATA_SHAPE + 3] =
                    QuantizeToInt8(normalized_accel_y, input_scale, input_zero_point);
                target[buffer_index * DATA_SHAPE + 4] =
                    QuantizeToInt8(normalized_accel_z, input_scale, input_zero_point);

                // gyro 归一化后再量化
                target[buffer_index * DATA_SHAPE + 5] =
                    QuantizeToInt8(normalized_gyro_x, input_scale, input_zero_point);
                target[buffer_index * DATA_SHAPE + 6] =
                    QuantizeToInt8(normalized_gyro_y, input_scale, input_zero_point);
                target[buffer_index * DATA_SHAPE + 7] =
                    QuantizeToInt8(normalized_gyro_z, input_scale, input_zero_point);

                target[buffer_index * DATA_SHAPE + 8] =
                    QuantizeToInt8(imuFeatureSet.vert_line_accel, input_scale, input_zero_point);

                target[buffer_index * DATA_SHAPE + 9] =
                    QuantizeToInt8(imuFeatureSet.accel_mag, input_scale, input_zero_point);

                target[buffer_index * DATA_SHAPE + 10] =
                    QuantizeToInt8(imuFeatureSet.hor_accel, input_scale, input_zero_point);

                target[buffer_index * DATA_SHAPE + 11] =
                    QuantizeToInt8(imuFeatureSet.vert_percent, input_scale, input_zero_point);

    #else

                /// 归一化后
                target[buffer_index * DATA_SHAPE + 0] = imuFeatureSet.roll;
                target[buffer_index * DATA_SHAPE + 1] = imuFeatureSet.pitch;

                target[buffer_index * DATA_SHAPE + 2] = imuFeatureSet.accel_x;
                target[buffer_index * DATA_SHAPE + 3] = imuFeatureSet.accel_y;
                target[buffer_index * DATA_SHAPE + 4] = imuFeatureSet.accel_z;

                target[buffer_index * DATA_SHAPE + 5] = imuFeatureSet.gyro_x;
                target[buffer_index * DATA_SHAPE + 6] = imuFeatureSet.gyro_y;
                target[buffer_index * DATA_SHAPE + 7] = imuFeatureSet.gyro_z;

                target[buffer_index * DATA_SHAPE + 8] =
                    imuFeatureSet.vert_line_accel;

                target[buffer_index * DATA_SHAPE + 9] =
                    imuFeatureSet.accel_mag;
                target[buffer_index * DATA_SHAPE + 10] =
                    imuFeatureSet.hor_accel;
                target[buffer_index * DATA_SHAPE + 11] =
                    imuFeatureSet.vert_percent;

    #endif
#endif

#if TEST_SHOW_ESP_GET_TIME_FLAG
                // [推理时间]记录AI推理数据包发送间隔
                int64_t current_time =
                    esp_timer_get_time() / 1000; // 转换为毫秒
                if (last_fusion_time != 0) {
                    int64_t interval = current_time - last_fusion_time;
                    printf("[AI推理] 数据包发送间隔: %lld ms", interval);
                }
                last_fusion_time = current_time;
#endif
                buffer_index++;

                //////
                // 检查是否需要切换缓冲区
                if (buffer_index >= INFERENCE_DATA_NUM) {
#if ENABLE_DEDUCTION_FLAG
                    static int elementCount = 0; // 元素计数器
                    static const int AI_BUFFER_BLOCK = AI_BUFFER_SIZE * DATA_SHAPE; // AI数据块的元素数
#if TFLITE_INT8_NORMALIZE_FLAG
                    int8_t *aiBuffer = nullptr;
#else
                    float *aiBuffer = nullptr;
#endif
                    if (BLE::g_isStartDeduction.load()) {
                        aiBuffer =
                            ai_active_buffer ? ai_buffer_ping : ai_buffer_pong;
                        // 验证指针有效性
                        assert(aiBuffer != NULL);
                        assert(esp_ptr_external_ram(aiBuffer));
                        assert((uintptr_t)aiBuffer % 16 == 0);

                        // 累积到AI缓冲区
                        if (aiBuffer && esp_ptr_external_ram(aiBuffer)) {
                            // 计算可复制的元素数量
                            int elementsToCopy = (INFERENCE_DATA_NUM * DATA_SHAPE); // 50*3
                            // 预先分配空间
                            int remainingSpace = AI_BUFFER_BLOCK - elementCount;
                            // 100*3 //
                            // 从初始位置开始
                            // 确保不超过缓冲区大小
                            if (elementsToCopy > remainingSpace) {                   // 50*3 > 100 * 3
                                elementsToCopy = remainingSpace;
                            }
// 复制数据
#if TFLITE_INT8_NORMALIZE_FLAG
                            // 计算字节偏移量
                            size_t offset = elementCount * sizeof(int8_t);
                            dma_memcpy((uint8_t *)aiBuffer + offset, target,elementsToCopy * sizeof(int8_t));
#else
                            // 计算字节偏移量
                            size_t offset = elementCount * sizeof(float);
                            dma_memcpy((uint8_t *)aiBuffer + offset, target,elementsToCopy * sizeof(float));
#endif
                            // 验证指针有效性
                            assert(aiBuffer != NULL);
                            assert(esp_ptr_external_ram(aiBuffer));
                            assert((uintptr_t)aiBuffer % 16 == 0);

                            elementCount += elementsToCopy;

                            // 检查是否达到目标元素数量
                            if (elementCount >= AI_BUFFER_BLOCK) {
#if TFLITE_INT8_NORMALIZE_FLAG
                                dma_memcpy((uint8_t *)aiBuffer_copy, aiBuffer,ai_buffer_block);
#else
                                dma_memcpy((uint8_t *)aiBuffer_copy, aiBuffer,ai_buffer_block);
#endif
                                // 验证指针有效性
                                assert(aiBuffer_copy != NULL);
                                assert(esp_ptr_external_ram(aiBuffer_copy));
                                assert((uintptr_t)aiBuffer_copy % 16 == 0);

                                printf("--------AI Infere \n");

                                if(deduce_interval == DEDUCE_INTERVAL){
                                    // 发送完整AI缓冲区
                                    BaseType_t sendAIStatus = xQueueSend(aiInferQueue, &aiBuffer_copy,pdMS_TO_TICKS(5));
                                    if (sendAIStatus != pdTRUE) {
                                        ESP_LOGW("TAG", "AI缓冲区发送失败");
                                    }
                                }
                                deduce_interval--;
                                if(deduce_interval ==0){
                                    deduce_interval =DEDUCE_INTERVAL;
                                }

                                // 切换前清除新缓冲区
#if TFLITE_INT8_NORMALIZE_FLAG
                                int8_t *ai_new_target = ai_active_buffer ? ai_buffer_ping : ai_buffer_pong;
                                memset(ai_new_target, 0, ai_buffer_block);
#else
                                float *ai_new_target = ai_active_buffer ? ai_buffer_ping : ai_buffer_pong;
                                memset(ai_new_target, 0, ai_buffer_block);
#endif
                                ai_active_buffer = !ai_active_buffer;
                                elementCount = 0;
                            }
                        }

                        // 骑行最小缓冲区
                        pitch_rpm.min_signal_range = 10;
                        // 步行最小缓冲区
                        pitch_rpm.min_signal_range = 4.0;

                        roll_rpm.min_signal_range = 10;
                        // 步行最小缓冲区
                        roll_rpm.min_signal_range = 4.0;

#if TFLITE_INT8_NORMALIZE_FLAG
                        // rpm = computeRPMToBLE_i(1, 0.0f, target);
                        rpm = computeRPMToBLE_i(g_inferenceType.load(),g_inferenceProb.load(),target);
#else
                        // rpm = computeRPMToBLE_f(1, 0.0f, target);
                        rpm = computeRPMToBLE_f(g_inferenceType.load(),
                        g_inferenceProb.load(),target);
#endif

                        printf("IMU temperature : %.2f\n",
                                      sanitizeValue(imuData.temperature));

#if TEST_SHOW_FREE_SPIRAM_FLAG
                        size_t free_psram =
                            heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
                        printf("[Data Fusion] Free PSRAM: %d bytes\n",
                                      free_psram);
#endif
                    }
#endif
                // 切换前清除新缓冲区
#if TFLITE_INT8_NORMALIZE_FLAG
                    int8_t *new_target = active_buffer ? buffer_ping : buffer_pong;
#else
                    float *new_target = active_buffer ? buffer_ping : buffer_pong;
#endif
                    memset(new_target, 0, buffer_block);
                    active_buffer = !active_buffer; // 切换缓冲区
                    buffer_index = 0;               // 重置索引
                }

                if (BLE::g_isStartDeduction.load()) {
                    FusionPacket packet = {
                        .roll = imuFeatureSet.roll,
                        .pitch = imuFeatureSet.pitch,
                        .yaw = imuFeatureSet.yaw,

                        .accel_x = imuFeatureSet.accel_x,
                        .accel_y = imuFeatureSet.accel_y,
                        .accel_z = imuFeatureSet.accel_z,

                        .gyro_x = imuFeatureSet.gyro_x,
                        .gyro_y = imuFeatureSet.gyro_y,
                        .gyro_z = imuFeatureSet.gyro_z,

                        .vert_line_accel = imuFeatureSet.vert_line_accel,

                        .accel_mag = imuFeatureSet.accel_mag,
                        .hor_accel = imuFeatureSet.hor_accel,
                        .vert_percent = imuFeatureSet.vert_percent,

                        .quat_x = sanitizeValue(imuData.rawData.quat.x()),
                        .quat_y = sanitizeValue(imuData.rawData.quat.y()),
                        .quat_z = sanitizeValue(imuData.rawData.quat.z()),
                        .quat_w = sanitizeValue(imuData.rawData.quat.w()),

                        .rpm = rpm,
                        .type = g_inferenceType.load(),

                        .timestamp = 0
                        };
                        // printf("%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                        //               packet.roll, packet.pitch, packet.yaw,
                        //               packet.accel_x, packet.accel_y, packet.accel_z);
                        add_to_buffer(packet);
                }else{      
                }

                ///////
                xSemaphoreGive(fusion_mutex);
            } else {
                ESP_LOGW("TAG", "缓冲区切换超时");
            }
#if !ENABLE_DEDUCTION_FLAG
            // 存入缓冲区
            if (!WiFiAP::bufferAddPacket(packet)) {
                ESP_LOGE("ARDUINO_LOG", "Failed to add packet to buffer");
            } else {
#if TEST_SHOW_ESP_GET_TIME_FLAG
                int64_t current_time = esp_timer_get_time() / 1000;
                if (last_packet_time != 0) {
                    int64_t interval = current_time - last_packet_time;
                    printf("[WiFi] 数据包发送间隔: %lld ms", interval);
                }
                last_packet_time = current_time;
#endif
                /***********************************************************************************************************
                 */
            }
#endif
#if TEST_SHOW_FREE_STACK_FLAG
            // 输出任务堆栈使用情况
            UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
            printf("[Data Fusion] Task Stack Free: %d\n",
                          stack * sizeof(StackType_t));
#endif
        }

        // 严格周期控制
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// 气压计任务
void taskBaroData(void *pvParam) {
    int baroCount = 0;
    printf("Baro task started");
    const TickType_t xFrequency = pdMS_TO_TICKS(50); // 50ms采样间隔
    TickType_t xLastWakeTime = xTaskGetTickCount();
    BaroData baroData = {0}; // 确保初始化为零
    bool readSuccess = false;
    static bool runStateFlag = false;

    while (1) {
        if (!PowerManager::is_active()) {
            vTaskSuspend(NULL); // 正确挂起任务
            continue;
        }

        static float pressureData[FILTER_SAMPLE_NUM] = {0};
        static uint8_t pressureIndex = 0;

        // 优化互斥锁使用
        if (xSemaphoreTake(baroMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
            baroData = baro.readValue();
            xSemaphoreGive(baroMutex);
        } else {
            ESP_LOGW("ARDUINO_LOG", "Baro mutex timeout");
        }

#if ENABLE_DEDUCTION_FLAG
        if (BLE::g_isStartDeduction.load()) {
            if (runStateFlag == false) {
                runStateFlag = true;
                baro.setBasePressure(baroData.pressure);  // 设置基准气压
                baro.setHumidity(BLE::g_humidity.load()); // 设置湿度
                baro.setBaseTemperature(
                    BLE::g_temperature.load()); // 设置环境温度

                // 上一次从网络中获取的温度
                lastTemperature = BLE::g_temperature;
                // 上一次从网络中获取的湿度
                lastHumidity = BLE::g_humidity;
                // 上一次从网络中获取的气压值
                lastPressure = BLE::g_surfacePressure;
                // 设置气压计温度
                baroTemperature.store(baroData.temperature); // 气压计温度
                printf("Set base pressure: %.2f hPa", baroData.pressure);
                if (taskStorageHandle != NULL) {
                    xTaskNotifyGive(taskStorageHandle);
                }
            }

            if (pressureIndex < FILTER_SAMPLE_NUM) {
                pressureData[pressureIndex] = baroData.pressure;
                pressureIndex++;

                if (baroCount % 20 == 0) {
                    // printf("Baro temperature : %.2f \n",
                    // baroData.temperature);
                    baroCount %= 20;
                }
                baroCount++;
            } else {
                for (int i = 0; i < FILTER_SAMPLE_NUM; i++) {
                    butterFilter.addData(pressureData[i]);
                }

                // 重置周期
                static int circleIndex = 0;
                // 上一次高度
                static float lastHeight = 0.0f;

                // 每60秒重置基准气压
                float bestPressure = butterFilter.processAndGetBestPoint();
                float relative_altitude =
                    baro.getCompensatedHeight(bestPressure);
                // 增加或者减小的不明显则视作位置没变;如果正向大于气压计分辨率则累加其高度，
                // 并且将该位置的高度值作为基准;如果反向大于气压计分辨率则仅仅更新该位置的高度值。
                if (relative_altitude - lastHeight > BARO_RESOLUTION) {
                    g_climbHeight.store(g_climbHeight.load() +
                                        (relative_altitude - lastHeight));
                    // 上一次高度
                    lastHeight = relative_altitude;
                } else {
                    if (relative_altitude - lastHeight < -BARO_RESOLUTION) {
                        // 上一次高度
                        lastHeight = relative_altitude;
                    }
                }

                if (circleIndex == MS5611_CIRCLE_PERIOD) {
                    {
                        baro.setHumidity(80);
                        baro.setBaseTemperature(25);
                        float deltaHeight =
                            baro.getSurfaceAltitude(1003.0, 1002.0);
                        printf("deltaHeight : %.2f \n", deltaHeight);
                    }

                    // 比较气压计测量的气压，更新基准气压
                    // 判断是否有气压变化，如果有则更新基准气压，阈值是0.01f
                    if (abs(bestPressure - baro.getBasePressure()) > 0.01f) {
                        // 设置当前压力值为基准气压,不断改变其高度参考值
                        baro.setBasePressure(bestPressure);
                        baro.setHumidity(BLE::g_humidity.load());
                        baro.setBaseTemperature(BLE::g_temperature.load());
                    }
                    lastHeight = 0.0f;
                    circleIndex = 0;
                    // Prefences 保存数据
                    if (taskStorageHandle != NULL) {
                        xTaskNotifyGive(taskStorageHandle);
                    }

                    if (BLE::g_bUpdate.load() &&
                        (abs(lastTemperature - BLE::g_temperature) > 0.1f ||
                         abs(lastHumidity - BLE::g_humidity) > 0.1f ||
                         abs(lastPressure - BLE::g_surfacePressure) > 0.1f)) {
                        baro.setHumidity(BLE::g_humidity.load());
                        baro.setBaseTemperature(BLE::g_temperature.load());
                        if (BLE::g_surfacePressure < lastPressure) {
                            float deltaHeight =
                                deltaHeight_Sum.load() +
                                baro.getSurfaceAltitude(BLE::g_surfacePressure,
                                                        lastPressure);
                            deltaHeight_Sum.store(deltaHeight);
                        }

                        printf("deltaHeight_Sum: %.2f m\n",
                                      deltaHeight_Sum.load());

                        lastTemperature = BLE::g_temperature;
                        lastHumidity = BLE::g_humidity;
                        lastPressure = BLE::g_surfacePressure;
                        BLE::g_bUpdate.store(false);
                    }
                } else {
                    circleIndex++;
                }

                // 获取海拔高度
                baro.setSeaLevelPressure(BLE::g_seaLevelPressure.load());
                baro.setHumidity(BLE::g_humidity.load());
                baro.setBaseTemperature(BLE::g_temperature.load());
                float altitude = baro.getAltitude(bestPressure);

                pressureIndex = 0;
                AltitudeInfo altitudeInfo;
                altitudeInfo.altitude = baroData.pressure;  // altitude;
                altitudeInfo.cilmbhigh = g_climbHeight.load() + deltaHeight_Sum;
                baroTemperature.store(baroData.temperature); // 气压计温度
                if (xQueueSend(baroDataQueue, &altitudeInfo,
                               pdMS_TO_TICKS(5)) != pdTRUE) {
                }
                memset(pressureData, 0, sizeof(pressureData));
                butterFilter.reset();
            }
        } else {
            if (runStateFlag == true) {
                runStateFlag = false;
                g_climbHeight.store(0.0f);
                deltaHeight_Sum.store(0.0f);
                BLE::g_sportCount.store(0.0f);

                pitch_rpm.last_rpm = 0.0f;
                roll_rpm.last_rpm = 0.0f;

                if (taskStorageHandle != NULL) {
                    xTaskNotifyGive(taskStorageHandle);
                }
            }

            if (BLE::g_bUpdate.load()) {
                baro.setBasePressure(baroData.pressure);  // 设置基准气压
                baro.setHumidity(BLE::g_humidity.load()); // 设置湿度
                baro.setBaseTemperature(
                    BLE::g_temperature.load()); // 设置环境温度
                BLE::g_bUpdate.store(false);
            }
        }
#endif
        // 严格周期控制
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void taskBatchSender(void *pvParam) {
#if !ENABLE_DEDUCTION_FLAG
    const TickType_t xBlockTime = pdMS_TO_TICKS(5);

    while (1) {
        FusionBatch batch;
        if (WiFiAP::getCompleteBatch(batch)) {
            // 发送到WiFi队列
            if (xQueueSend(wifiDataQueue, &batch, xBlockTime) != pdTRUE) {
                // ESP_LOGE("ARDUINO_LOG", "Batch %u send failed",
                // batch.sequence);
                //// 可选重试逻辑
            } else {
                // printf("Sent batch %u (%u packets)",
                // batch.sequence,BATCH_SIZE);
            }
        }

        // 流量控制：根据缓冲区使用率调整
        uint8_t used = ((batchBuffer.wr_ptr - batchBuffer.rd_ptr) %
                        (BUFFER_DEPTH * BATCH_SIZE)) /
                       BATCH_SIZE;
        if (used > BUFFER_DEPTH / 2) {
            vTaskDelay(pdMS_TO_TICKS(1)); // 高负载时降频
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
#endif
}

float computeRPMToBLE_i(uint8_t predicted_class, float max_prob,
                      int8_t *input_data_copy) {
    // printf("预测类别: %d", predicted_class);
    if (input_data_copy == nullptr) {
        ESP_LOGE("ARDUINO_LOG", "Input data copy is null");
        return 0.0f;
    }

    AltitudeInfo altitudeInfo;
    if (xQueueReceive(baroDataQueue, &altitudeInfo, pdMS_TO_TICKS(0)) == pdTRUE) {
    }

    std::string res = "Mode:";
    switch (predicted_class) {
    case SportType::NOKNOW:
        res += "noknow";
        break;
    case SportType::WALKING:
        res += "walking";
        break;
    case SportType::BYCLING:
        res += "bycling";
        break;
    case SportType::JOGGING:
        res += "jogging";
        break;
    default:
        res += "未匹配到动作";
        break;
    }

    float rtn = 0.0f;
    static bool condition = false;
    static int writeIndex = 0;
    static float rpm_f = 0.0f;
    // if (predicted_class == 2 || predicted_class == 1) {
        // 当为户外骑行时
        // 更新缓冲区（线程安全）
        for (int index = 0; index < INFERENCE_DATA_NUM; index++) {
#if TFLITE_INT8_NORMALIZE_FLAG
            roll_rpm.angle_buffer[writeIndex] =
                (input_data_copy[index * DATA_SHAPE] - input_zero_point) *
                input_scale * 180.0f; // Roll角度

            pitch_rpm.angle_buffer[writeIndex] =
                (input_data_copy[index * DATA_SHAPE + 1] - input_zero_point) *
                input_scale * 90.0f; // Pitch角度
#endif
            writeIndex = (writeIndex + 1) % WINDOW_BUFFER_SIZE;
        }


        // 变量定义
        // ============================================================
        float pitch_var = 0.0f;
        float roll_var = 0.0f;
        float raw_rpm_pitch = 0.0f; 
        float raw_rpm_roll = 0.0f;
        
        // 核心参数
        const float MIN_VARIANCE_THRESHOLD = 5.0f;
        const float MAX_RPM_JUMP = 15.0f;
        
        // 状态记忆变量 
        static float last_valid_rpm = 0.0f;      
        static int consecutive_noise_count = 0;  

        // ============================================================
        // 第一步：计算方差和原始 RPM 
        // ============================================================
        if (writeIndex == 0) {
            pitch_var = RPM::calculateVariance(pitch_rpm.angle_buffer, WINDOW_BUFFER_SIZE);
            roll_var = RPM::calculateVariance(roll_rpm.angle_buffer, WINDOW_BUFFER_SIZE);
            raw_rpm_pitch = pitch_rpm.calculateRPM();
            raw_rpm_roll = roll_rpm.calculateRPM();
        } else { 
            pitch_var = RPM::calculateVariance(pitch_rpm.angle_buffer, writeIndex);
            roll_var = RPM::calculateVariance(roll_rpm.angle_buffer, writeIndex);
            raw_rpm_pitch = pitch_rpm.calculateRPM(false, writeIndex);
            raw_rpm_roll = roll_rpm.calculateRPM(false, writeIndex);
        }

        // ============================================================
        // 第二步：比较方差并决策 (去掉了 detected_stop 标志位)
        // ============================================================
        
        float final_rpm = 0.0f;

        auto is_physically_plausible = [&](float new_rpm) -> bool {
            if (last_valid_rpm == 0.0f) return true; 
            return fabsf(new_rpm - last_valid_rpm) <= MAX_RPM_JUMP;
        };

        // [改进] 优先检查：如果 FFT 已经在底层做了硬刹车 (Raw=0)，或者方差极低
        // 直接判定 final_rpm 为 0，交给第三步去处理
        if ((raw_rpm_pitch == 0.0f && raw_rpm_roll == 0.0f) || 
            (roll_var < MIN_VARIANCE_THRESHOLD && pitch_var < MIN_VARIANCE_THRESHOLD)) {
            final_rpm = 0.0f;
        } 
        else {
            bool p_is_plausible = is_physically_plausible(raw_rpm_pitch) && (raw_rpm_pitch > 0.0f);
            bool r_is_plausible = is_physically_plausible(raw_rpm_roll) && (raw_rpm_roll > 0.0f);

            if (p_is_plausible && r_is_plausible) {
                final_rpm = (pitch_var > roll_var) ? raw_rpm_pitch : raw_rpm_roll;
            }
            else if (p_is_plausible) {
                final_rpm = raw_rpm_pitch;
            }
            else if (r_is_plausible) {
                final_rpm = raw_rpm_roll;
            }
            else {
                // 都不靠谱的情况
                float best_guess = 0.0f;
                if (raw_rpm_pitch > 0.0f && pitch_var > roll_var) best_guess = raw_rpm_pitch;
                else if (raw_rpm_roll > 0.0f) best_guess = raw_rpm_roll;

                if (best_guess > 0.0f && last_valid_rpm > 0.0f) {
                    // 钳位逻辑
                    if (best_guess > last_valid_rpm) final_rpm = last_valid_rpm + MAX_RPM_JUMP;
                    else final_rpm = last_valid_rpm - MAX_RPM_JUMP;
                } else if (best_guess > 0.0f) {
                    final_rpm = best_guess; // 启动第一帧
                } else {
                    final_rpm = 0.0f; // 彻底算不出来
                }
            }
        }

        // ============================================================
        // 第三步：EMA 平滑与历史更新 (简化版)
        // ============================================================
        
        if (final_rpm > 0.0f) {
            // [运动中]
            consecutive_noise_count = 0; // 重置静止计数
            
            float alpha = 0.7f; 
            if (last_valid_rpm == 0.0f) {
                last_valid_rpm = final_rpm;
            } else {
                last_valid_rpm = (alpha * final_rpm) + ((1.0f - alpha) * last_valid_rpm);
            }
        } 
        else {
            // [判定为停止] (final_rpm == 0)
            // 只要这一帧算出来是 0 (无论是FFT硬刹车，还是方差过小)，就立刻进入停止逻辑
            consecutive_noise_count++;
            
            // 阈值设为 1，意味着不需要额外的防抖延时
            // 只要检测到一次 0，就立刻归零。这能最大程度消除你说的 "末尾2个Block"
            if (consecutive_noise_count >= 1) {
                last_valid_rpm = 0.0f;
            }
            // 否则保持 last_valid_rpm 不变 (Hold 1帧，防止极其偶然的掉帧)
        }

        // 更新最终输出变量
        rpm_f = last_valid_rpm;

        // 打印结果
        printf("RPM: %.2f\n", rpm_f);

        // ============================================================
        // 第四步：更新数据处理向量 (保持原样)
        // ============================================================
        if (writeIndex == 0) {
            pitch_rpm.dealRPMVector(rpm_f, writeIndex);
            roll_rpm.dealRPMVector(rpm_f, writeIndex); 
        } else {
            pitch_rpm.dealRPMVector(rpm_f, writeIndex, false);
            roll_rpm.dealRPMVector(rpm_f, writeIndex, false);
        }


        condition = false;
    // } else {
    //     // 当不为户外骑行时，则踏频重新统计
    //     if (condition == true) {
    //         // 更新缓冲区（线程安全）
    //         if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(2))) {

    //             memset(pitch_rpm.angle_buffer, 0,
    //                    WINDOW_BUFFER_SIZE * sizeof(float));
    //             memset(roll_rpm.angle_buffer, 0,
    //                    WINDOW_BUFFER_SIZE * sizeof(float));

    //             xSemaphoreGive(bufferMutex);
    //         }
    //         writeIndex = 0;
    //     }
    //     condition = false;
    // }

        rtn = rpm_f;  
        if (BLE::deviceConnected.load()) {
        //     // 准备配置参数
        //     BLEStringBuilderConfig config{.baseString = res,
        //                                 .maxProb = max_prob,
        //                                 .rpm = rpm_f,
        //                                 .altitudeInfo = altitudeInfo,
        //                                 .condition = condition};

        // // 构建并发送数据
        // buildAndSendBLEString(config);

        DataPacket dp;
        dp.timestamp = esp_timer_get_time() / 1000;
        dp.type = predicted_class;
        dp.duration = 135;
        dp.battery = battery_soc.load();
        dp.altitude = altitudeInfo.altitude; // Pa
        dp.ascentHeight= g_climbHeight.load();
        dp.rpm = rpm_f;
        dp.sportLaps = (uint32_t)BLE::g_sportCount.load();
        dp.prob = max_prob;
        std::vector<uint8_t> frame = sendTelemetry(dp);
        sendFrameToQueue(frame.data(), frame.size());

        rpm_f = 0.0f;
    }
    return rtn;
}

float computeRPMToBLE_f(uint8_t predicted_class, float max_prob,
                      float *input_data_copy) {
    // printf("预测类别: %d", predicted_class);
    if (input_data_copy == nullptr) {
        ESP_LOGE("ARDUINO_LOG", "Input data copy is null");
        return 0.0f;
    }

    AltitudeInfo altitudeInfo;
    if (xQueueReceive(baroDataQueue, &altitudeInfo, pdMS_TO_TICKS(0)) ==
        pdTRUE) {
    }

    std::string res = "Mode:";
    switch (predicted_class) {
    case SportType::NOKNOW:
        res += "noknow";
        break;
    case SportType::WALKING:
        res += "walking";
        break;
    case SportType::BYCLING:
        res += "bycling";
        break;
    case SportType::JOGGING:
        res += "jogging";
        break;
    default:
        res += "未匹配到动作";
        break;
    }

    float rtn = 0.0f;
    static bool condition = false;
    static int writeIndex = 0;
    static float rpm_f = 0.0f;
    // if (predicted_class == 2 || predicted_class == 1) {
        // 当为户外骑行时
        // 更新缓冲区（线程安全）
        for (int index = 0; index < INFERENCE_DATA_NUM; index++) {
            roll_rpm.angle_buffer[writeIndex] =
                input_data_copy[index * DATA_SHAPE]; // Roll角度

            pitch_rpm.angle_buffer[writeIndex] =
                input_data_copy[index * DATA_SHAPE + 1]; // Pitch角度

            writeIndex = (writeIndex + 1) % WINDOW_BUFFER_SIZE;
        }

        // 变量定义
        // ============================================================
        float pitch_var = 0.0f;
        float roll_var = 0.0f;
        float raw_rpm_pitch = 0.0f; 
        float raw_rpm_roll = 0.0f;
        
        // 核心参数
        const float MIN_VARIANCE_THRESHOLD = 5.0f;
        const float MAX_RPM_JUMP = 15.0f;
        
        // 状态记忆变量 
        static float last_valid_rpm = 0.0f;      
        static int consecutive_noise_count = 0;  

        // ============================================================
        // 第一步：计算方差和原始 RPM 
        // ============================================================
        if (writeIndex == 0) {
            pitch_var = RPM::calculateVariance(pitch_rpm.angle_buffer, WINDOW_BUFFER_SIZE);
            roll_var = RPM::calculateVariance(roll_rpm.angle_buffer, WINDOW_BUFFER_SIZE);
            raw_rpm_pitch = pitch_rpm.calculateRPM();
            raw_rpm_roll = roll_rpm.calculateRPM();
        } else { 
            pitch_var = RPM::calculateVariance(pitch_rpm.angle_buffer, writeIndex);
            roll_var = RPM::calculateVariance(roll_rpm.angle_buffer, writeIndex);
            raw_rpm_pitch = pitch_rpm.calculateRPM(false, writeIndex);
            raw_rpm_roll = roll_rpm.calculateRPM(false, writeIndex);
        }

        // ============================================================
        // 第二步：比较方差并决策 (去掉了 detected_stop 标志位)
        // ============================================================
        
        float final_rpm = 0.0f;

        auto is_physically_plausible = [&](float new_rpm) -> bool {
            if (last_valid_rpm == 0.0f) return true; 
            return fabsf(new_rpm - last_valid_rpm) <= MAX_RPM_JUMP;
        };

        // [改进] 优先检查：如果 FFT 已经在底层做了硬刹车 (Raw=0)，或者方差极低
        // 直接判定 final_rpm 为 0，交给第三步去处理
        if ((raw_rpm_pitch == 0.0f && raw_rpm_roll == 0.0f) || 
            (roll_var < MIN_VARIANCE_THRESHOLD && pitch_var < MIN_VARIANCE_THRESHOLD)) {
            final_rpm = 0.0f;
        } 
        else {
            bool p_is_plausible = is_physically_plausible(raw_rpm_pitch) && (raw_rpm_pitch > 0.0f);
            bool r_is_plausible = is_physically_plausible(raw_rpm_roll) && (raw_rpm_roll > 0.0f);

            if (p_is_plausible && r_is_plausible) {
                final_rpm = (pitch_var > roll_var) ? raw_rpm_pitch : raw_rpm_roll;
            }
            else if (p_is_plausible) {
                final_rpm = raw_rpm_pitch;
            }
            else if (r_is_plausible) {
                final_rpm = raw_rpm_roll;
            }
            else {
                // 都不靠谱的情况
                float best_guess = 0.0f;
                if (raw_rpm_pitch > 0.0f && pitch_var > roll_var) best_guess = raw_rpm_pitch;
                else if (raw_rpm_roll > 0.0f) best_guess = raw_rpm_roll;

                if (best_guess > 0.0f && last_valid_rpm > 0.0f) {
                    // 钳位逻辑
                    if (best_guess > last_valid_rpm) final_rpm = last_valid_rpm + MAX_RPM_JUMP;
                    else final_rpm = last_valid_rpm - MAX_RPM_JUMP;
                } else if (best_guess > 0.0f) {
                    final_rpm = best_guess; // 启动第一帧
                } else {
                    final_rpm = 0.0f; // 彻底算不出来
                }
            }
        }

        // ============================================================
        // 第三步：EMA 平滑与历史更新 (简化版)
        // ============================================================
        
        if (final_rpm > 0.0f) {
            // [运动中]
            consecutive_noise_count = 0; // 重置静止计数
            
            float alpha = 0.7f; 
            if (last_valid_rpm == 0.0f) {
                last_valid_rpm = final_rpm;
            } else {
                last_valid_rpm = (alpha * final_rpm) + ((1.0f - alpha) * last_valid_rpm);
            }
        } 
        else {
            // [判定为停止] (final_rpm == 0)
            // 只要这一帧算出来是 0 (无论是FFT硬刹车，还是方差过小)，就立刻进入停止逻辑
            consecutive_noise_count++;
            
            // 阈值设为 1，意味着不需要额外的防抖延时
            // 只要检测到一次 0，就立刻归零。这能最大程度消除你说的 "末尾2个Block"
            if (consecutive_noise_count >= 1) {
                last_valid_rpm = 0.0f;
            }
            // 否则保持 last_valid_rpm 不变 (Hold 1帧，防止极其偶然的掉帧)
        }

        // 更新最终输出变量
        rpm_f = last_valid_rpm;

        // 打印结果
        printf("RPM: %.2f\n", rpm_f);

        // ============================================================
        // 第四步：更新数据处理向量 (保持原样)
        // ============================================================
        if (writeIndex == 0) {
            pitch_rpm.dealRPMVector(rpm_f, writeIndex);
            roll_rpm.dealRPMVector(rpm_f, writeIndex); 
        } else {
            pitch_rpm.dealRPMVector(rpm_f, writeIndex, false);
            roll_rpm.dealRPMVector(rpm_f, writeIndex, false);
        }

        condition = true;
    // } 
    // else {
    //     // 当不为户外骑行时，则踏频重新统计
    //     if (condition == true) {
    //         // 更新缓冲区（线程安全）
    //         if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(2))) {
    //             memset(pitch_rpm.angle_buffer, 0,
    //                    WINDOW_BUFFER_SIZE * sizeof(float));
    //             memset(roll_rpm.angle_buffer, 0,
    //                    WINDOW_BUFFER_SIZE * sizeof(float));
    //             xSemaphoreGive(bufferMutex);
    //         }
    //         writeIndex = 0;
    //     }

    //     condition = false;
    // }

        rtn = rpm_f;
        if (BLE::deviceConnected.load()) {
        //     // 准备配置参数
        //     BLEStringBuilderConfig config{.baseString = res,
        //                                 .maxProb = max_prob,
        //                                 .rpm = rpm_f,
        //                                 .altitudeInfo = altitudeInfo,
        //                                 .condition = condition};

        // // 构建并发送数据
        // buildAndSendBLEString(config);

        DataPacket dp;
        dp.timestamp = esp_timer_get_time() / 1000;
        dp.type = predicted_class;
        dp.duration = 135;
        dp.battery = battery_soc.load();
        dp.altitude = altitudeInfo.altitude; // Pa
        dp.ascentHeight= g_climbHeight.load();
        dp.rpm = rpm_f;
        dp.sportLaps = (uint32_t)BLE::g_sportCount.load();
        dp.prob = max_prob;
        std::vector<uint8_t> frame = sendTelemetry(dp);
        sendFrameToQueue(frame.data(), frame.size());

        rpm_f = 0.0f;
    }
    return rtn;
}

// AI推理任务（中优先级）
void taskAIInference(void *pvParam) {
    ESP_LOGI(
        "ARDUINO_LOG",
        "---------------------------------- taskAIInference --------------");
    static int64_t last_infere_time = 0; // 记录上一次推理发送时间
#if ENABLE_DEDUCTION_FLAG
    while (1) {
        if (!PowerManager::is_active()) {
            vTaskSuspend(NULL); // 在低功耗时挂起
            continue;
        }

#if TFLITE_INT8_NORMALIZE_FLAG
        int8_t *psram_input = nullptr;
#else
        float *psram_input = nullptr;
#endif

        int64_t start_time = esp_timer_get_time() / 1000;
        // 等待姿态角数据包（最多等待100ms）
        if (xQueueReceive(aiInferQueue, &psram_input, pdMS_TO_TICKS(600)) ==
            pdTRUE) {
            int32_t curr_time = esp_timer_get_time() / 1000;
            if (psram_input &&
                esp_ptr_external_ram(psram_input)) { // 双重验证指针有效性
                // 执行推理前加锁
                if (xSemaphoreTake(tflite_mutex, pdMS_TO_TICKS(600)) ==
                    pdTRUE) {
                    // 未验证分配后的地址是否对齐
                    assert((uintptr_t)psram_input % 16 == 0 &&
                           "Memory not aligned!");

#if TFLITE_INT8_NORMALIZE_FLAG
                    dma_memcpy((uint8_t *)input->data.int8, psram_input, input->bytes);
                    int8_t *psram_output = output->data.int8;
#else
                    dma_memcpy((uint8_t *)input->data.f, psram_input, input->bytes);
                    float *psram_output = output->data.f;
#endif
                    memset(psram_output, 0, output->bytes);
                    // 执行推理
                    TfLiteStatus status = interpreter->Invoke();
                    if (status != kTfLiteOk) {
                        ESP_LOGE("ARDUINO_LOG", "推理失败: %d", status);
                        continue;
                    } else {
                        // 处理输出
                        int8_t predicted_class = 0;
                        float max_prob = 0.0;
                        for (uint8_t i = 0; i < 4; i++) {
#if TFLITE_INT8_NORMALIZE_FLAG
                            // 反量化输出
                            float prob = (psram_output[i] - output_zero_point) *
                                         output_scale;
#else
                            // 输出
                            float prob = psram_output[i];
#endif
                            if (prob > max_prob) {
                                max_prob = prob;
                                predicted_class = i;
                            }
                            printf("类别:%d,概率:%f", i, prob);
                        }

                        // 是否增加阈值判断，超过一定的值则认为是有效动作
                        if (max_prob < 0.4) { // 阈值
                            predicted_class = 0;
                            max_prob = 0; 
                        } 
                        g_inferenceType.store(predicted_class);
                        g_inferenceProb.store(max_prob);

#if TEST_SHOW_DEDUCE_TIME_FLAG
                        int64_t current_time = esp_timer_get_time() / 1000;
                        if (start_time != 0) {
                            int64_t interval = current_time - start_time;
                            printf("[AI Inference] AI推理时间间隔: %lld ms", interval);
                        }
                        last_infere_time = current_time;
#endif

                    }
#if TEST_SHOW_FREE_STACK_FLAG
                    // 输出任务堆栈使用情况
                    UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
                    printf("AI Interface Task Stack Free: %d\n",
                                  stack * sizeof(StackType_t));
#endif

                    xSemaphoreGive(tflite_mutex);
                } else {
                    ESP_LOGE("ARDUINO_LOG", "TFLite 互斥锁超时");
                }
            }
        }
    }
#endif
}

// WiFi通信任务
void taskWiFiComms(void *pvParam) {
#if !ENABLE_DEDUCTION_FLAG
    while (1) {
        FusionBatch batch;
        if (xQueueReceive(wifiDataQueue, &batch, portMAX_DELAY) == pdPASS) {
            // 分离互斥量作用域
            bool has_activity = false;
            if (xSemaphoreTake(wsMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                if (ws.count() > 0) {
                    ws.binaryAll((uint8_t *)&batch, sizeof(FusionBatch));
                    has_activity = true; // 标记需要更新活动时间
                }
                xSemaphoreGive(wsMutex);
            }

            // 独立更新活动时间（无需嵌套锁）
            if (has_activity) {
                const TickType_t now = xTaskGetTickCount();
                last_ws_activity_time.store(xTaskGetTickCount(),
                                            std::memory_order_relaxed);
            }
            // 性能统计
            static uint32_t last_seq = 0;
            if (last_seq != 0 && batch.sequence != last_seq + 1) {
                // ESP_LOGW("ARDUINO_LOG", "Packet loss detected! Prev:%d
                // Curr:%d", (int32_t)last_seq,
                //         (int32_t)batch.sequence);
            }
            last_seq = batch.sequence;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
#if TEST_SHOW_FREE_STACK_FLAG
    UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
    printf("[WIFI] Task Stack Free: %d\n", stack * sizeof(StackType_t));
#endif

#endif
}

void taskRGBLED(void *pvParam) {
    static float radian = 0.0f;
    const uint16_t cycleTime = 10000; // 10s完整呼吸周期
    const float radianStep = 0.01f;   // 弧度步进值
    static bool lastState = false;


    while (1) {
        if (!PowerManager::is_active()) {
            vTaskSuspend(NULL); // 在低功耗时挂起
            continue;
        }

        if (xSemaphoreTake(fastLED_mutex, pdMS_TO_TICKS(1)) ==
            pdTRUE) {
            if (BLE::g_isStartDeduction.load()) {
                // 初始化SD卡
                ledState.store(eGREEN); // 绿色
                lastState = true;
            }
            if(lastState && (BLE::g_isStartDeduction.load() == false))
            {
                ledState.store(eORANGE);              // 橙色
                lastState = false;
            }

            switch(ledState.load())
            {
                case eOFF:
                    fill_solid(leds, NUM_LEDS, CRGB::Black); // 关闭LED
                    FastLED.setBrightness(0);
                    FastLED.show();
                    break;
                case eBLUE:
                    fill_solid(leds, NUM_LEDS, CRGB::Blue);
                    break;
                case eGREEN:
                    fill_solid(leds, NUM_LEDS, CRGB::Green);
                    break;
                case eORANGE:
                    fill_solid(leds, NUM_LEDS, CRGB::Orange);
                    break;
                case eRED:
                    fill_solid(leds, NUM_LEDS, CRGB::Red);
                    break;
                case ePURPLE:
                    fill_solid(leds, NUM_LEDS, CRGB::Purple);
                    break;
                case eGRAY:
                    fill_solid(leds, NUM_LEDS, CRGB::Gray);
                    break;
                case eYELLOW:
                    fill_solid(leds, NUM_LEDS, CRGB::Yellow);
                    break;
                case eWHITE:
                    fill_solid(leds, NUM_LEDS, CRGB::White);
                    break;
                default:
                    fill_solid(leds, NUM_LEDS, CRGB::Green);
                    break;
            }

            if(ledState.load()!= eOFF){
                radian += radianStep;
                if (radian > 2 * PI)
                    radian -= 2 * PI;

                uint8_t brightness = (sin(radian) + 1) * 127.5;
                FastLED.setBrightness(brightness);
                FastLED.show();   
            }
            xSemaphoreGive(fastLED_mutex);
        } 

        vTaskDelay(pdMS_TO_TICKS(cycleTime * radianStep / (2 * PI))); // 动态延时
    }
}

//  低功耗管理任务
void taskPowerCheck(void *pvParam) {
    static bool lastMode(BLE::g_manualLowPowerMode.load());
    while (1) {
        if (lastMode != BLE::g_manualLowPowerMode.load()) {
            if (BLE::g_manualLowPowerMode.load()) {

            }
        }
        lastMode = BLE::g_manualLowPowerMode.load();
        vTaskDelay(pdMS_TO_TICKS(500));
    }

#if TEST_SHOW_FREE_STACK_FLAG
    UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
    printf("[Power] Task Stack Free: %d\n", stack * sizeof(StackType_t));
#endif
}

//  电池电量检测任务
void taskBatteryCheck(void *pvParam) {
    while (1) {
        if (!PowerManager::is_active()) {
            vTaskSuspend(NULL); // 在低功耗时挂起
            continue;
        }

        if(lipo.isConnected())
        {
            float soc = lipo.getSOC();
            float voltage = lipo.getVoltage();
            if(soc > SOC_THRESHOLD){
                lipo.clearAlert();
            }

            // 电池电量
            battery_soc = soc;
            // std::string SOC_str = "SOC:" + std::to_string(battery_soc) +
            //                     ",Temp:" + std::to_string(baroTemperature.load());
            // std::string *result = new std::string(SOC_str);
            // if (xQueueSend(bleDataQueue, &result, 0) != pdTRUE) {
            //     delete result;
            //     result = nullptr;
            // };
            battery_voltage.store(voltage);
            printf("Battery Voltage : %.2f\n", voltage);
            printf("Soc : %.2f\n", soc);

            if(battery_soc > 98.0)
            {
                // 充电失能
                digitalWrite(CE_PIN, LOW);
            }
            else
            {
                //充电使能
                digitalWrite(CE_PIN, HIGH);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000 * 10));
    }
}

// 存储任务函数
void taskStorage(void *pvParameters) {
    const char *name_space = "smart_wear"; // 固定命名空间

    while (1) {
        // 等待存储操作信号
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 打开命名空间并执行存储操作
        prefs.begin(name_space, false);
        prefs.putBool("run_state", BLE::g_isStartDeduction.load()
                                       ? true
                                       : false); // 保存运行状态
        prefs.putFloat("climb_height", BLE::g_isStartDeduction.load()
                                           ? g_climbHeight.load()
                                           : 0.0f); // 保存爬升高度
        prefs.putFloat("sport_laps", BLE::g_isStartDeduction.load()
                                         ? BLE::g_sportCount.load()
                                         : 0.0f); // 保存运动圈数
        prefs.putFloat("sea_level_pressure", baro.getSeaLevelPressure());
        prefs.putInt("file_counter", file_counter);
        prefs.end(); // 保存并关闭

        printf("run_state:%d", BLE::g_isStartDeduction.load() ? 1 : 0);
        printf("climb_height:%.2f", BLE::g_isStartDeduction.load()
                                               ? g_climbHeight.load()
                                               : 0.0f);
        printf("sport_laps:%.2f", BLE::g_isStartDeduction.load()
                                             ? BLE::g_sportCount.load()
                                             : 0.0f);
        printf("sea_level_pressure:%.2f", baro.getSeaLevelPressure());
        printf("file_counter:%d", file_counter);
    }
}
} // namespace TaskManager