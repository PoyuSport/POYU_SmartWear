#pragma once

extern "C" { // 必须包裹 C 语言头文件
#include "esp_log.h"
}

#include "SW_define.h"
#include "driver/adc.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_wifi.h"

//#include <esp_gap_ble_api.h>
#include <functional>
// 使用原子操作
#include <atomic>

extern SemaphoreHandle_t pmMutex;  

#ifdef __cplusplus
extern "C" {
#endif

  void vApplicationIdleHook(void) __attribute__((weak));

class PowerManager {  
  public:
    static void init();

    static void update_activity_timestamp();

    // 修改电源管理临界区保护
    static bool enter_active_mode();
    // 进入低功耗模式
    static bool enter_low_power_sleep_mode();

    static bool manage_power_state();

    static void handle_power_transition();

    static void configure_radio_power();

    // CPU 动态调频策略
    static void configure_cpu_freq();

    static void force_sleep();

    static bool is_active();

    // 注册回调函数
    static void set_device_callbacks(std::function<void()> wake_cb,
                              std::function<void()> sleep_cb);

private:
    // 禁止实例化
    PowerManager() = delete;
    ~PowerManager() = delete;
};
#ifdef __cplusplus
}
#endif
