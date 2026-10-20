#ifndef BLE_H
#define BLE_H

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include <esp_gap_ble_api.h>

#include <esp_sleep.h>

// 使用原子布尔变量
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
// 在文件顶部添加分区操作头文件
#include <esp_partition.h>
#include <esp_ota_ops.h>

#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>

#include <SW_struct.h>

#include <esp_sleep.h>
#include <soc/rtc.h>
#include <driver/rtc_io.h>

#include <Preferences.h>

// 定义BLE服务UUID
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

static const char* BLE_TAG = "OTA";
/// Preferences 存储  NVS
extern Preferences prefs;

extern "C" { // 必须包裹 C 语言头文件
#include "esp_log.h"
}

namespace BLE
{   
    enum BLE_State {
        BLE_IDLE,        // 已初始化但未广播
        BLE_ADVERTISING, // 正在广播
        BLE_CONNECTED    // 已连接设备
    };

    struct BleCommand {
        uint8_t data[256];  // 固定大小缓冲区
        size_t length;
    };

    // 全局队列和内存池
    extern QueueHandle_t commandQueue;
    extern BleCommand* commandPool;
    extern uint8_t poolSlot;

    extern std::atomic<BLE_State> bleState;    
    extern std::atomic<bool> deviceConnected; // 或者添加互斥锁

    static TaskHandle_t commandTaskHandle = NULL;

    /********************** BLE传入参数 *************************/
    // 开始推理 | true 表示运动开始;false 表示运动结束
    extern std::atomic<bool> g_isStartDeduction; 
    // 开始运动计数
    extern std::atomic<float> g_sportCount;
    // 室外温度
    extern std::atomic<float> g_temperature;
    // 海平面气压
    extern std::atomic<float> g_seaLevelPressure; // 标准大气压
    // 表面大气压
    extern std::atomic<float> g_surfacePressure;  // 表面大气压
    // 湿度
    extern std::atomic<float> g_humidity;
    // 进入低功耗模式/正常模式
    extern std::atomic<bool> g_manualLowPowerMode;
    // 是否更新最新的气压信息
    extern std::atomic<bool> g_bUpdate;
    /************************************************************/
    extern SemaphoreHandle_t bleMutex;
    // 使用静态变量减少栈分配
    static SemaphoreHandle_t receive_mutex = xSemaphoreCreateMutex();  // 互斥锁改为静态创建

    // 内存池配置
    constexpr uint8_t COMMAND_POOL_SIZE = 5;  // 5个预分配命令槽位
    constexpr uint8_t COMMAND_MAX_LENGTH = 255;

    // BLE GAP参数配置
    static esp_ble_adv_params_t adv_params = {
        .adv_int_min = 0x20,
        .adv_int_max = 0x40,
        .adv_type = ADV_TYPE_IND,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };

    // gap事件处理函数
    void gap_event_handler(esp_gap_ble_cb_event_t event,
                           esp_ble_gap_cb_param_t *param);
    // 初始化内存池
    void initCommandPool();
    // 初始化BLE设备
    void initBLEDevice();
    // 初始化BLE
    void initBLE();
    // 关闭BLE
    void closeBLE();
    // 挂起BLE服务 
    void bleSuspend();
    // 恢复BLE服务
    void bleResume();
    // 字符串 -- 安全发送数据
    void safeSendData(const std::string &value);
    // 二进制协议 -- 安全发送数据
    void safeSendFrame(const uint8_t *data, size_t len);
    // 固件更新任务 (推荐通过任务调用)
    void updateFirmwareTask(void* pvParameters);
    // 命令进程任务
    void commandProcessorTask(void *pvParameters);
    // 处理命令
    void handleCommand(const char *cmd, size_t len);
    // 解析浮点数
    float parseFloat(const char *str);

    void restoreTimeFromNVS();

    void saveTimeToNVS(time_t timestamp);

    // 增强型连接回调
    class EnhancedServerCallbacks : public BLEServerCallbacks {
    private:
      void updateConnParamsWithRetry(esp_bd_addr_t bda);
    public:
      void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param);
      void onDisconnect(BLEServer *pServer);
    };

    class BLESensorCharacteristicCallbacks : public BLECharacteristicCallbacks {
    public:
        void onWrite(BLECharacteristic *pCharacteristic);
    };
}
#endif