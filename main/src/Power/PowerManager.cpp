
#include <PowerManager.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <string.h>
#include <algorithm>
#include "driver/adc.h"
#include "esp_freertos_hooks.h"
#include "esp_log.h"

#define CONFIG_PM_ENABLE 1
#define CONFIG_FREERTOS_USE_TICKLESS_IDLE 1

// 电源管理锁
static esp_pm_lock_handle_t pm_lock;
static SemaphoreHandle_t pm_mutex;
static const char* TAG = "PowerManager";

// 电源状态变量
static std::atomic<bool> active_mode(true);
static std::atomic<uint32_t> last_activity_time(0);
static const uint32_t IDLE_TIMEOUT = 5000; // 5秒无活动进入睡眠

// 回调函数
static std::function<void()> device_wake_callback = nullptr;
static std::function<void()> device_sleep_callback = nullptr;

// FreeRTOS空闲钩子函数
extern "C" {
    void vApplicationIdleHook(void) {   
        if (!active_mode && pm_lock != nullptr) {
            ESP_LOGD(TAG, "Entering light sleep from idle hook");
            esp_light_sleep_start();
        }
    }

    void PowerManager::init() {
        // 创建互斥量
        pm_mutex = xSemaphoreCreateMutex();
        
        // 创建电源管理锁 (禁止轻睡眠)
        esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "pm_lock", &pm_lock);
        
        // 初始状态下，我们获取锁以保持系统活跃
        esp_pm_lock_acquire(pm_lock);
        active_mode.store(true);
        
        // 配置CPU频率策略 (但不启用自动轻睡眠)
        configure_cpu_freq();
        
        ESP_LOGI(TAG, "Power manager initialized");
    }

    void PowerManager::update_activity_timestamp() {
        uint32_t new_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        last_activity_time.store(new_time);
        ESP_LOGD(TAG, "Activity updated: %lu", new_time);
    }

    bool PowerManager::enter_active_mode() {
        if (xSemaphoreTake(pm_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 获取锁，阻止睡眠
            if (pm_lock != nullptr) {
                esp_pm_lock_acquire(pm_lock);
            }
            
            // 更新状态
            active_mode.store(true);
            
            // 调用唤醒回调
            if (device_wake_callback) {
                device_wake_callback();
            }
            
            // 更新活动时间戳
            update_activity_timestamp();
            
            xSemaphoreGive(pm_mutex);
            ESP_LOGD(TAG, "Entered active mode");
            return true;
        }
        return false;
    }

    bool PowerManager::enter_low_power_sleep_mode() {
        if (xSemaphoreTake(pm_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 释放锁，允许睡眠
            if (pm_lock != nullptr) {
                esp_pm_lock_release(pm_lock);
            }
            
            // 更新状态
            active_mode.store(false);
            
            // 调用睡眠回调
            if (device_sleep_callback) {
                device_sleep_callback();
            }
            
            xSemaphoreGive(pm_mutex);
            ESP_LOGD(TAG, "Entered low power mode");
            return true;
        }
        return false;
    }

    // 注册回调函数
    void PowerManager::set_device_callbacks(std::function<void()> wake_cb,
                                std::function<void()> sleep_cb) {
        device_wake_callback = wake_cb;
        device_sleep_callback = sleep_cb;
    }

    void PowerManager::configure_radio_power() {
    #if !ENABLE_DEDUCTION_FLAG
        // WiFi 节能配置
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    #else
        BLEAdvertisementData advertisementData;
        advertisementData.setName("ESP32-S3_BLE");
        // BLE 连接参数优化
        esp_ble_conn_update_params_t params = {
            .min_int = 6, .max_int = 24, .latency = 0, .timeout = 100};
        esp_ble_gap_update_conn_params(&params);
    #endif
    }

    // CPU 动态调频策略
    void PowerManager::configure_cpu_freq() {
        esp_pm_config_esp32s3_t pm_config = {
            .max_freq_mhz = 160,
            .min_freq_mhz = 40,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
            .light_sleep_enable = true 
#endif
        };
        esp_pm_configure(&pm_config);

        // 配置睡眠时保持RTC外设和晶振供电
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
        esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_ON);
        
        // 使能GPIO在睡眠时保持状态
        esp_sleep_enable_gpio_switch(true);
        
        ESP_LOGI(TAG, "CPU frequency configured");
    }

    // 强制进入睡眠（无论当前状态如何）
    void PowerManager::force_sleep() {
        enter_low_power_sleep_mode();
        // 立即进入睡眠，而不是等待空闲钩子
        esp_light_sleep_start();
    }

    // 检查当前是否处于活跃模式
    bool PowerManager::is_active() {
        return active_mode.load();
    }
}