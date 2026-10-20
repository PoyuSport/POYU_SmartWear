#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAddress.h>
#include <BLE.h>
#include <memory>

// #include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_sleep.h>
#include <esp_pm.h>
#include <soc/rtc.h>
#include <driver/rtc_io.h>

/// Preferences 存储  NVS
Preferences prefs;

namespace BLE
{
    /********************** BLE传入参数 *************************/
    // 开始推理 | true 表示运动开始;false 表示运动结束
    std::atomic<bool> g_isStartDeduction(false); 
    // 开始运动计数
    std::atomic<float> g_sportCount(0.0f);
    // 室外温度
    std::atomic<float> g_temperature(25.0f);
    // 海平面气压
    std::atomic<float> g_seaLevelPressure(1013.25); // 标准大气压
    // 表面大气压
    std::atomic<float> g_surfacePressure(1013.25);  // 表面大气压
    // 湿度
    std::atomic<float> g_humidity(0.0f);

    // 人工进入低功耗模式/正常模式
    std::atomic<bool> g_manualLowPowerMode(false);

    // 是否更新最新的气压信息
    std::atomic<bool> g_bUpdate(false);
    // BLE剩余栈空间
    std::atomic<uint32_t> g_bleFreeStackSize(0);

    /************************************************************/
    std::atomic<bool> deviceConnected(false);
    std::atomic<BLE_State> bleState(BLE_IDLE);
    QueueHandle_t commandQueue = nullptr;
    BleCommand* commandPool = nullptr;
    uint8_t poolSlot = 0;

    BLECharacteristic *pSensorCharacteristic = nullptr;

    BLEServer *pServer = nullptr; // 将服务器声明为全局变量以便管理

    SemaphoreHandle_t bleMutex = xSemaphoreCreateMutex();
    // 全局互斥锁声明（在文件作用域）
    static SemaphoreHandle_t g_commandMutex = xSemaphoreCreateMutex();
    static TaskHandle_t g_otaTaskHandle = NULL;  // OTA任务句柄

    static esp_bd_addr_t g_bd_addr; 

    #define BLE_DEVICE_NAME "POYUSPORT"
    /************************************************************/

    typedef struct {
        rtc_slow_freq_t original_slow_clk_freq;
        bool was_external_32k_enabled;
        // 可选：记录睡眠时的电源配置，用于调试
        esp_sleep_pd_option_t sleep_rtc_periph_pd;
        esp_sleep_pd_option_t sleep_xtal_pd;
    } rtc_clock_state_t;

    rtc_clock_state_t original_clock_state;

    void initCommandPool() {
        // 创建队列（存储指针而非整个结构体）
        commandQueue = xQueueCreate(10, sizeof(BleCommand*));

        // 在PSRAM中分配内存池
        commandPool = (BleCommand*)heap_caps_malloc(
            COMMAND_POOL_SIZE * sizeof(BleCommand),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );

        // 创建队列（存储指针而非整个结构体）
        if (commandPool) {
            for (int i = 0; i < COMMAND_POOL_SIZE; i++) {
                commandPool[i].length = 0;
                memset(commandPool[i].data, 0, COMMAND_MAX_LENGTH);
            }
        } else {
            ESP_LOGE("BLE", "PSRAM分配失败，回退到内部RAM");
            commandPool = new BleCommand[COMMAND_POOL_SIZE];
        }
    }

    static bool ble_wakeup_flag = false;
    // BLE事件处理
    void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
        if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
            esp_ble_gap_start_advertising(&adv_params);
        }else if(event == ESP_GAP_BLE_SCAN_RESULT_EVT){
            ble_wakeup_flag = true; 
            Serial.println("light-sleep 唤醒！");
        }else if(event == ESP_GATTS_DISCONNECT_EVT){

        }
    }

    void initBLEDevice() {

        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

        // 配置蓝牙控制器（关键：启用BLE睡眠模式）
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        bt_cfg.sleep_mode = ESP_BT_SLEEP_MODE_1; // 必须设置为模式1
        bt_cfg.sleep_clock = ESP_BT_SLEEP_CLOCK_EXT_32K_XTAL;
        esp_bt_controller_init(&bt_cfg);
        esp_bt_controller_enable(ESP_BT_MODE_BLE);

        // 在蓝牙控制器初始化后，启用睡眠
        esp_err_t ret = esp_bt_sleep_enable();
        if (ret != ESP_OK) {
            //Serial.printf("启用蓝牙睡眠失败: %s\n", esp_err_to_name(ret));
        } else {
            Serial.println("蓝牙睡眠已启用");
        }

        // 初始化BLE设备
        BLEDevice::init("POYUSPORT");
        BLEDevice::setMTU(128); // 增大MTU提升吞吐量

        // 注册GAP回调（底层）
        esp_ble_gap_register_callback(gap_event_handler);

        // 创建服务器
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new EnhancedServerCallbacks());

        // 创建服务
        BLEService *pService = pServer->createService(SERVICE_UUID);

        // 配置特征值
        pSensorCharacteristic = pService->createCharacteristic(
            CHARACTERISTIC_UUID,BLECharacteristic::PROPERTY_WRITE 
                                | BLECharacteristic::PROPERTY_READ 
                                | BLECharacteristic::PROPERTY_NOTIFY);

        // 必须添加通知描述符
        BLE2902 *pDescriptor = new BLE2902();
        pDescriptor->setNotifications(true);
        pSensorCharacteristic->addDescriptor(pDescriptor);

        pSensorCharacteristic->setValue("Hello ESP32-S3 BLE!");
        pSensorCharacteristic->setCallbacks(new BLESensorCharacteristicCallbacks());
        pService->start();

        // 配置广播参数
        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(SERVICE_UUID);

        // 关键广播参数设置
        pAdvertising->setMinInterval(0x20); // 20ms (32*0.625ms)
        pAdvertising->setMaxInterval(0x40); // 40ms (64*0.625ms)
        pAdvertising->setScanResponse(true);

        // 自定义广播数据包
        BLEAdvertisementData advData;
        advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC |
                        ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
        advData.setCompleteServices(BLEUUID(SERVICE_UUID));
        advData.setName("ESP32_BLE"); // 强制包含完整名称
        pAdvertising->setAdvertisementData(advData);

        // 开始广播
        BLEDevice::startAdvertising();
        // 启动初始广播
        bleResume();
    }

    void initBLE(){
        initBLEDevice();
        // 初始化内存池
        initCommandPool();

    }

    void closeBLE() {
        if (pSensorCharacteristic) {
            pSensorCharacteristic->setCallbacks(nullptr);
        }

        BLEDevice::deinit(true); // 彻底释放资源
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
        pServer = nullptr;
        deviceConnected.store(false);
        bleState.store(BLE_IDLE);
        Serial.println("BLE已关闭");
        vTaskDelay(pdMS_TO_TICKS(100));
    }


    // 安全数据发送函数（优化版）
    void safeSendData(const std::string &value) {
        static SemaphoreHandle_t send_mutex = xSemaphoreCreateMutex();
        if (!send_mutex)
            return;

        if (xSemaphoreTake(send_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            const uint8_t MAX_RETRY = 3;
            const uint8_t BASE_DELAY_MS = 15;

            for (uint8_t retry = 0; retry <= MAX_RETRY; retry++) {
                if (deviceConnected.load()) {
                    printf("发送数据: %s", value.c_str());
                    std::string val = "poyu#"+value + '\n';
                    pSensorCharacteristic->setValue(String(val.c_str()));
                    pSensorCharacteristic->notify(); // 假设 notify() 返回 bool
                    xSemaphoreGive(send_mutex);
                    return;
                    // 发送失败时继续重试
                }

                // 退避逻辑
                if (retry < MAX_RETRY) {
                    uint16_t delay_ms = BASE_DELAY_MS * (1 << retry);
                    vTaskDelay(pdMS_TO_TICKS(delay_ms));
                }
            }
            xSemaphoreGive(send_mutex);
        }
    }

    void safeSendFrame(const uint8_t* data, size_t len) {
        static SemaphoreHandle_t send_mutex = xSemaphoreCreateMutex();

        if (!send_mutex || !data || len == 0) {
            return;
        }

        if (xSemaphoreTake(send_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            const uint8_t MAX_RETRY = 3;
            const uint16_t BASE_DELAY_MS = 15;

            for (uint8_t retry = 0; retry <= MAX_RETRY; retry++) {
                if (deviceConnected.load()) {

                    pSensorCharacteristic->setValue(data, len);
                    pSensorCharacteristic->notify();

                    printf("BLE send raw frame: %u bytes\n", len);

                    xSemaphoreGive(send_mutex);
                    return;
                }

                if (retry < MAX_RETRY) {
                    uint16_t delay_ms = BASE_DELAY_MS << retry;
                    vTaskDelay(pdMS_TO_TICKS(delay_ms));
                }
            }

            xSemaphoreGive(send_mutex);
        }
    }



    void setTemperature(float value) { 
        g_temperature.store(value); 
    }

    // 包装函数
    void setSeaLevelPressure(float value) {
        g_seaLevelPressure.store(value);
    }

    void setSurfacePressure(float value) { g_surfacePressure.store(value); }

    void setHumidity(float value) {
        g_humidity.store(value);
    }

    void handleCommand(const char* cmd, size_t len) {
        // 使用联合解析结构体（栈外分配）
        struct CmdParser {
            const char* key;
            void (*setter)(float);
        };
        static const CmdParser parsers[] = {
            {"Temperature=", setTemperature},
            {"Pressure=", setSeaLevelPressure},
            {"SurfPressure=",setSurfacePressure},
            {"Humidity=", setHumidity}
        };

        for (const auto& parser : parsers) {
            const char* pos = strstr(cmd, parser.key);
            if (pos != nullptr) {
                if (parser.setter != nullptr) {
                    parser.setter(parseFloat(pos + strlen(parser.key)));
                }
                break; // 找到即退出
            }
        }
    }

    // 优化7：强制内联解析函数
    __attribute__((always_inline)) 
    float parseFloat(const char* str) {
        return strtof(str, nullptr); // 简化错误处理（根据需求恢复）
    }

    // 处理UNIX时间戳命令
    void processTimeCommand(const char* timestampData) {
        Serial.printf("[TIME] 收到时间戳命令: %s\n", timestampData);

        // 解析时间戳格式: "Time=1705300225"
        time_t timestamp = 0;
        if (sscanf(timestampData + 5, "%lld", &timestamp) == 1) {
            // 设置系统时间
            struct timeval tv = {
                .tv_sec = timestamp,
                .tv_usec = 0
            };

            if (settimeofday(&tv, NULL) == 0) {
                // 转换为可读时间并打印
                struct tm timeinfo;
                localtime_r(&timestamp, &timeinfo);

                Serial.printf("[TIME] 时间戳校准成功: %04d-%02d-%02d %02d:%02d:%02d\n",
                            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

                // 保存到NVS
                saveTimeToNVS(timestamp);

                // 发送确认消息回小程序
                char ackMsg[64];
                snprintf(ackMsg, sizeof(ackMsg), "TimeSet: %04d-%02d-%02d %02d:%02d:%02d", 
                        timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                safeSendData(std::string(ackMsg));


            } else {
                Serial.println("[TIME] 时间戳设置失败");
            }
        } else {
            Serial.println("[TIME] 时间戳格式解析失败");
        }
    }

    void commandProcessorTask(void *pvParameters) {
        UBaseType_t minFreeStack = UINT_MAX;
        for (;;) {
            BleCommand* cmd = nullptr;
            if (xQueueReceive(commandQueue, &cmd, portMAX_DELAY) == pdPASS) {
                static char cmd_buffer[COMMAND_MAX_LENGTH + 1];
                const size_t copy_len = (cmd->length < COMMAND_MAX_LENGTH) ? cmd->length : COMMAND_MAX_LENGTH;
                memcpy(cmd_buffer, cmd->data, copy_len);
                cmd_buffer[copy_len] = '\0';

                // 更新栈监控（单位：字节）
                UBaseType_t freeStackWords = uxTaskGetStackHighWaterMark(NULL);
                UBaseType_t freeStackBytes = freeStackWords * sizeof(StackType_t);
                if (freeStackBytes < minFreeStack) {
                    minFreeStack = freeStackBytes;
                    ESP_LOGI("STACK", "Min free stack: %u bytes", minFreeStack);
                }
                // 命令处理
                if (memcmp(cmd_buffer, "Firmware", 8) == 0) {
                    // 确保只存在一个OTA任务
                    if (g_otaTaskHandle == NULL) {
                        xTaskCreate(updateFirmwareTask, "ota_task", 4096, 
                                NULL, 2, &g_otaTaskHandle);
                    }
                } 
                else 
                {
                    // 全局变量保护锁
                    xSemaphoreTake(g_commandMutex, portMAX_DELAY);

                    if (memcmp(cmd_buffer, "Start", 5) == 0) {
                        g_isStartDeduction.store(true);
                        g_sportCount.store(0.0f);
                        Serial.printf("----> Start Deduction\n");
                    }
                    else if (memcmp(cmd_buffer, "Finish", 6) == 0) {
                        g_isStartDeduction.store(false);
                        Serial.printf("----> Finish Deduction\n");  
                    }
                    else if (memcmp(cmd_buffer, "State", 5) == 0) {
                        // 避免动态内存分配
                        std::string stateMsg = g_isStartDeduction.load() ? "State:1" : "State:0";
                        safeSendData(stateMsg);
                    }
                    else if(memcmp(cmd_buffer,"Stack",5) == 0){
                        std::string stackStr = "stack = " + std::to_string(g_bleFreeStackSize.load());
                        safeSendData(stackStr);
                    }
                    else if(memcmp(cmd_buffer,"Power=1",7) == 0){
                        g_manualLowPowerMode.store(true);
                    }
                    else if(memcmp(cmd_buffer,"Power=0",7) == 0){
                        g_manualLowPowerMode.store(false);
                    }
                    else if(memcmp(cmd_buffer,"Time=",5) == 0){
                        processTimeCommand(cmd_buffer);
                    }
                    else {
                        handleCommand(cmd_buffer, copy_len);
                        g_bUpdate.store(true);
                    }

                    xSemaphoreGive(g_commandMutex);
                }
            }
        }
    }

    // 固件更新任务 (推荐通过任务调用)
    void updateFirmwareTask(void* pvParameters) {
        ESP_LOGE(BLE_TAG, "--- updateFirmwareTask ---");
        // 获取当前分区
        const esp_partition_t* current = esp_ota_get_running_partition();
        if (!current) {
            ESP_LOGE(BLE_TAG, "current partition failed");
            safeSendData("[OTA] ERROR : current partition failed");
            vTaskDelete(NULL);
            return;
        }

        // 获取目标分区（强制指定 OTA 类型）
        const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
        if (!target) {
            ESP_LOGE(BLE_TAG, "NOT FOUND OTA PARTITION");
            safeSendData("[OTA] ERROR : NOT FOUND OTA PARTITION");
            vTaskDelete(NULL);
            return;
        }

        // 安全验证
        if (!current || !target) {
            safeSendData("ERROR : PARTITION ERROR");
            return;
        }

        if (current == target) {
            target = esp_ota_get_next_update_partition(target); // 切换到另一个分区
        }

        bleSuspend();
        // 设置启动分区
        esp_err_t err = esp_ota_set_boot_partition(target);
        bleResume();  // 立即恢复 BLE

        // 处理结果
        if (err != ESP_OK) {
            // ESP_LOGE(BLE_TAG, "START PARTITION FAILED: %s", esp_err_to_name(err));
            safeSendData("[OTA] ERROR:START PARTITION FAILED: ");
        } else {
            //ESP_LOGI(BLE_TAG, "PARTITION FAILED : TARGET: %s (0x%x)", target->label, target->address);

            char result[64];
            // snprintf(result, sizeof(result), 
            //         "[OTA] SUCCESSED !RESTART PARTION:\n%s (0x%x)", 
            //         target->label, target->address);
            safeSendData(result);

            //安全延迟重启（可选）
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
        vTaskDelete(NULL);
    }

    // 安全挂起函数（仅停止广播）
    void bleSuspend() {
        if (xSemaphoreTake(bleMutex, pdMS_TO_TICKS(500))) {
            if (bleState.load() == BLE_ADVERTISING) {
                Serial.println("[BLE] 停止广播...");
                BLEDevice::stopAdvertising();
                bleState.store(BLE_IDLE);
            }
            xSemaphoreGive(bleMutex);
        }
    }

    // 安全恢复函数（仅启动广播）
    void bleResume() {
        if (xSemaphoreTake(bleMutex, pdMS_TO_TICKS(500))) {
            if (bleState.load() == BLE_IDLE) {
                Serial.println("[BLE] 启动广播...");
                BLEDevice::startAdvertising();
                bleState.store(BLE_ADVERTISING);
            }
            xSemaphoreGive(bleMutex);
        }
    }

    // 保存时间到NVS
    void saveTimeToNVS(time_t timestamp) {
        prefs.begin("smart_wear", false);

        prefs.putULong("last_sync", timestamp);
        prefs.putULong("boot_time", esp_timer_get_time() / 1000);

        prefs.end();

        ESP_LOGI("TIME", "时间已保存到NVS: %lld", timestamp);
    }

    // 从NVS恢复时间（在setup中调用）
    void restoreTimeFromNVS() {
        prefs.begin("smart_wear", true);

        time_t savedTime = prefs.getULong("last_sync", 0);
        unsigned long bootTime = prefs.getULong("boot_time", 0);

        prefs.end();

        printf("savedTime: %lld, bootTime: %lu\n", savedTime, bootTime);

        if (savedTime > 0 || bootTime > 0) {
            // 计算经过的时间并恢复
            time_t currentTime = savedTime + (esp_timer_get_time() / 1000  - bootTime) / 1000;
            struct timeval tv = { .tv_sec = currentTime, .tv_usec = 0 };
            settimeofday(&tv, NULL);

            ESP_LOGI("TIME", "从NVS恢复时间成功");

            // 显示恢复的时间
            struct tm timeinfo;
            localtime_r(&currentTime, &timeinfo);
            ESP_LOGI("TIME", "恢复的时间: %04d-%02d-%02d %02d:%02d:%02d",
                    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        } else {
            ESP_LOGW("TIME", "未找到有效的时间数据");
        }
    }
    void EnhancedServerCallbacks::updateConnParamsWithRetry(esp_bd_addr_t bda) {
        memcpy(g_bd_addr, bda, sizeof(esp_bd_addr_t)); // 保存连接句柄
        const int MAX_RETRIES = 3;
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, bda, 6);
        conn_params.latency = 0;
        conn_params.min_int = 0x20;
        conn_params.max_int = 0x40;
        conn_params.timeout = 400;

        for (int i = 0; i < MAX_RETRIES; i++) {
            esp_err_t err = esp_ble_gap_update_conn_params(&conn_params);
            if (err == ESP_OK) return;

            Serial.printf("参数更新尝试 %d 失败 (错误: %d), 2秒后重试\n", i+1, err);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        Serial.println("连接参数更新失败，已达最大重试次数");
    }

    void EnhancedServerCallbacks::onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) {
        updateConnParamsWithRetry(param->connect.remote_bda);
        xSemaphoreTake(bleMutex, portMAX_DELAY);
        if (bleState.load() == BLE_ADVERTISING) {
            deviceConnected.store(true);
            bleState.store(BLE_CONNECTED);
            Serial.println("[BLE] 设备已连接");
        }
        xSemaphoreGive(bleMutex);
    }

    void EnhancedServerCallbacks::onDisconnect(BLEServer *pServer) {
        xSemaphoreTake(bleMutex, portMAX_DELAY);
        deviceConnected.store(false);
        bleState.store(BLE_IDLE);
        xSemaphoreGive(bleMutex);

        Serial.println("[BLE] 设备断开，保持协议栈活跃");
        bleResume(); // 自动恢复广播
    }

    void BLESensorCharacteristicCallbacks::onWrite(BLECharacteristic *pCharacteristic) {
        // 仅获取数据和长度 - 几乎不消耗栈空间
        const uint8_t* raw_data = pCharacteristic->getData();
        size_t data_len = pCharacteristic->getLength();

        // 优化1：提前获取栈监控数据
        UBaseType_t start_stack_free = uxTaskGetStackHighWaterMark(NULL);

            Serial.printf("[入口] 剩余栈空间: %d bytes", start_stack_free * sizeof(StackType_t));


        // 查找结束符（不创建临时变量）
        const uint8_t* end_ptr = raw_data;
        while (end_ptr < raw_data + data_len && *end_ptr != '\0') end_ptr++;
        size_t valid_len = end_ptr - raw_data;

        // 使用全局预分配内存池（零栈消耗）
        static BleCommand* cmd = nullptr;
        static size_t pool_size = 0;

        // 初始化内存池（仅第一次）
        if (pool_size == 0) {
            pool_size = COMMAND_POOL_SIZE;
            cmd = (BleCommand*)heap_caps_malloc(
                pool_size * sizeof(BleCommand), 
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
            );
        }

        // 轮转使用内存池中的槽位
        static uint8_t slot = 0;
        BleCommand* current_cmd = &cmd[slot];
        slot = (slot + 1) % pool_size;

        // 直接复制数据到预分配内存
        current_cmd->length = (valid_len < COMMAND_MAX_LENGTH) ? valid_len : COMMAND_MAX_LENGTH;
        memcpy(current_cmd->data, raw_data, current_cmd->length);
        current_cmd->data[current_cmd->length] = '\0';

        // 优化1：提前获取栈监控数据
        start_stack_free = uxTaskGetStackHighWaterMark(NULL);
        Serial.printf("[出口] 剩余栈空间: %d bytes", start_stack_free * sizeof(StackType_t));
        //ESP_LOGI("STACK", "cmd_buffer: %s", current_cmd);

        g_bleFreeStackSize.store(start_stack_free * sizeof(StackType_t));

        if (memcmp(&current_cmd, "Firmware", 8) == 0) {
            // bool bFirmware = true;
            // if (xQueueSendToBack(firmwareQueue, &bFirmware, 0) != pdPASS) 
            // {
            //     ESP_LOGE("BLE", "命令队列已满，丢弃命令");
            // }
        } 
        // 发送到队列（仅指针，零拷贝）
        if (xQueueSendToBack(commandQueue, &current_cmd, 0) != pdPASS) {
            ESP_LOGE("BLE", "命令队列已满，丢弃命令");
        }
    }
}