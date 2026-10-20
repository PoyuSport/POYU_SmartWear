#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <BLE.h>
#include <Initialize.h>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <SW_define.h>
#include <SW_struct.h>

#include "../system_diagnostics.h"

// 爬升高度
extern std::atomic<float> g_climbHeight;

extern std::atomic<double> battery_voltage;
extern std::atomic<double> battery_soc;

extern std::atomic<int16_t> buffer_index;

extern std::atomic<bool> g_WiFiIsConnected;

namespace TaskManager {
    // 新增全局变量
    extern TaskHandle_t taskFusionHandle;
    extern TaskHandle_t taskBaroHandle;
    extern TaskHandle_t taskIMUHandle;
    extern TaskHandle_t taskBLEHandle;
    extern TaskHandle_t taskAIHandle;
    extern TaskHandle_t taskBatteryHandle;
    // 声明全局任务句柄
    extern TaskHandle_t taskStorageHandle;
    extern TaskHandle_t taskFastLEDHandle;

    // 新增任务状态管理器
    class TaskStateManager {
    public:
        static void suspend_tasks() {
            ESP_LOGI("TAG", "休眠...");
            vTaskSuspend(taskBLEHandle);
            vTaskSuspend(taskFusionHandle);
            vTaskSuspend(taskBaroHandle);
            vTaskSuspend(taskIMUHandle);
            vTaskSuspend(taskBatteryHandle);
    #if ENABLE_DEDUCTION_FLAG
            vTaskSuspend(taskAIHandle);
    #endif
            FastLED.setBrightness(0);
            FastLED.show();
            vTaskSuspend(taskFastLEDHandle);
        }

        static void resume_tasks() {
            ESP_LOGI("TAG", "唤醒...");
            buffer_index = 0;
            if (taskBLEHandle &&
                (eTaskGetState(taskBLEHandle) == eSuspended)) {
                vTaskResume(taskBLEHandle);
            }
            if (taskFusionHandle &&
                (eTaskGetState(taskFusionHandle) == eSuspended)) {
                vTaskResume(taskFusionHandle);
            }
            if (taskBaroHandle && (eTaskGetState(taskBaroHandle) == eSuspended)) {
                vTaskResume(taskBaroHandle);
            }
            if (taskIMUHandle && (eTaskGetState(taskIMUHandle) == eSuspended)) {
                vTaskResume(taskIMUHandle);
            }
            if (taskBatteryHandle &&
                (eTaskGetState(taskBatteryHandle) == eSuspended)) {
                vTaskResume(taskBatteryHandle);
            }
            if (taskFastLEDHandle &&
                (eTaskGetState(taskFastLEDHandle) == eSuspended)) {
                vTaskResume(taskFastLEDHandle);
            }
    #if ENABLE_DEDUCTION_FLAG
            if (taskAIHandle && (eTaskGetState(taskAIHandle) == eSuspended)) {
                vTaskResume(taskAIHandle);
            }
    #endif
        }
    };

    // BLE通信任务（低优先级）
    void taskBLEComms(void *pvParam);

    // 数据融合与姿态解算任务
    void taskDataFusion(void *pvParam);

    // IMU采集任务
    void taskIMUData(void *pvParam);

    // 气压计任务
    void taskBaroData(void *pvParam);

    // 新增批量发送任务
    void taskBatchSender(void *pvParam);

    // AI推理任务
    void taskAIInference(void *pvParam);

    // WIFI通信任务（低优先级）
    void taskWiFiComms(void *pvParam);

    // RGBLED任务显示
    void taskRGBLED(void *pvParam);

    // 电源检查任务
    void taskPowerCheck(void *pvParam);

    //  电池电量检测任务
    void taskBatteryCheck(void *pvParam);

    // 存储任务 NVS
    void taskStorage(void *pvParam);

    // // OLED显示任务
    // void taskOLEDUpdate(void *pvParam){}

    float computeRPMToBLE_i(uint8_t predicted_class, float max_prob,
                        int8_t *input_data_copy);

    float computeRPMToBLE_f(uint8_t predicted_class, float max_prob,
                        float *input_data_copy);

} 

#endif TASK_MANAGER_H