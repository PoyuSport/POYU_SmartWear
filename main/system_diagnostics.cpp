#include "system_diagnostics.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <time.h>
#include <string.h>
#include <esp_log.h>

// ===== 全局变量定义 =====
SystemDiagnostics sys_diag;
reset_stats_t g_reset_stats;

// 日志标签
static const char* TAG = "SystemDiagnostics";

// ===== NVS复位统计实现 =====
#define NVS_NAMESPACE "sys_stats"
#define KEY_TOTAL_RESETS "total"
#define KEY_POWERON_RESETS "poweron"
#define KEY_SOFTWARE_RESETS "sw"
#define KEY_PANIC_RESETS "panic"
#define KEY_TASK_WDT_RESETS "task_wdt"
#define KEY_INT_WDT_RESETS "int_wdt"
#define KEY_BROWNOUT_RESETS "brownout"
#define KEY_DEEPSLEEP_WAKEUPS "deepsleep"
#define KEY_OTHER_RESETS "other"
#define KEY_LAST_REASON "last_reason"
#define KEY_LAST_TIMESTAMP "last_time"

// 初始化NVS
static esp_err_t init_nvs_if_needed(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

// 从NVS加载统计信息
static void load_stats_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err;
    
    err = init_nvs_if_needed();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS初始化失败: %s", esp_err_to_name(err));
        memset(&g_reset_stats, 0, sizeof(g_reset_stats));
        return;
    }
    
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "无法打开NVS命名空间，使用默认统计");
        memset(&g_reset_stats, 0, sizeof(g_reset_stats));
        return;
    }
    
    // 读取统计值
    nvs_get_u32(handle, KEY_TOTAL_RESETS, &g_reset_stats.total_resets);
    nvs_get_u32(handle, KEY_POWERON_RESETS, &g_reset_stats.poweron_resets);
    nvs_get_u32(handle, KEY_SOFTWARE_RESETS, &g_reset_stats.software_resets);
    nvs_get_u32(handle, KEY_PANIC_RESETS, &g_reset_stats.panic_resets);
    nvs_get_u32(handle, KEY_TASK_WDT_RESETS, &g_reset_stats.task_wdt_resets);
    nvs_get_u32(handle, KEY_INT_WDT_RESETS, &g_reset_stats.int_wdt_resets);
    nvs_get_u32(handle, KEY_BROWNOUT_RESETS, &g_reset_stats.brownout_resets);
    nvs_get_u32(handle, KEY_DEEPSLEEP_WAKEUPS, &g_reset_stats.deepsleep_wakeups);
    nvs_get_u32(handle, KEY_OTHER_RESETS, &g_reset_stats.other_resets);
    nvs_get_u32(handle, KEY_LAST_REASON, &g_reset_stats.last_reset_reason);
    nvs_get_u32(handle, KEY_LAST_TIMESTAMP, &g_reset_stats.last_reset_time);
    
    nvs_close(handle);
}

// 保存统计信息到NVS
static void save_stats_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err;
    
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "无法打开NVS命名空间进行保存");
        return;
    }
    
    // nvs_set_u32(handle, KEY_TOTAL_RESETS, 0);
    // nvs_set_u32(handle, KEY_POWERON_RESETS, 0);
    // nvs_set_u32(handle, KEY_SOFTWARE_RESETS, 0);
    // nvs_set_u32(handle, KEY_PANIC_RESETS, 0);
    // nvs_set_u32(handle, KEY_TASK_WDT_RESETS, 0);
    // nvs_set_u32(handle, KEY_INT_WDT_RESETS, 0);
    // nvs_set_u32(handle, KEY_BROWNOUT_RESETS, 0);
    // nvs_set_u32(handle, KEY_DEEPSLEEP_WAKEUPS, 0);
    // nvs_set_u32(handle, KEY_OTHER_RESETS, 0);
    // nvs_set_u32(handle, KEY_LAST_REASON, 0);
    // nvs_set_u32(handle, KEY_LAST_TIMESTAMP, 0);

    nvs_set_u32(handle, KEY_TOTAL_RESETS, g_reset_stats.total_resets);
    nvs_set_u32(handle, KEY_POWERON_RESETS, g_reset_stats.poweron_resets);
    nvs_set_u32(handle, KEY_SOFTWARE_RESETS, g_reset_stats.software_resets);
    nvs_set_u32(handle, KEY_PANIC_RESETS, g_reset_stats.panic_resets);
    nvs_set_u32(handle, KEY_TASK_WDT_RESETS, g_reset_stats.task_wdt_resets);
    nvs_set_u32(handle, KEY_INT_WDT_RESETS, g_reset_stats.int_wdt_resets);
    nvs_set_u32(handle, KEY_BROWNOUT_RESETS, g_reset_stats.brownout_resets);
    nvs_set_u32(handle, KEY_DEEPSLEEP_WAKEUPS, g_reset_stats.deepsleep_wakeups);
    nvs_set_u32(handle, KEY_OTHER_RESETS, g_reset_stats.other_resets);
    nvs_set_u32(handle, KEY_LAST_REASON, g_reset_stats.last_reset_reason);
    nvs_set_u32(handle, KEY_LAST_TIMESTAMP, g_reset_stats.last_reset_time);    

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提交NVS更改失败: %s", esp_err_to_name(err));
    }
    
    nvs_close(handle);
}

void update_reset_stats(esp_reset_reason_t reason) {
    // 加载现有统计
    load_stats_from_nvs();
    
    // 更新总重启计数
    g_reset_stats.total_resets++;
    g_reset_stats.last_reset_reason = reason;
    
    // 获取当前时间戳
    time_t now;
    time(&now);
    g_reset_stats.last_reset_time = (uint32_t)now;
    
    // 根据复位原因更新具体计数
    switch (reason) {
        case ESP_RST_POWERON:
            g_reset_stats.poweron_resets++;
            break;
        case ESP_RST_SW:
            g_reset_stats.software_resets++;
            break;
        case ESP_RST_PANIC:
            g_reset_stats.panic_resets++;
            ESP_LOGE(TAG, "系统崩溃！正在紧急保存统计...");
            save_stats_to_nvs();
            return;
        case ESP_RST_TASK_WDT:
            g_reset_stats.task_wdt_resets++;
            break;
        case ESP_RST_INT_WDT:
            g_reset_stats.int_wdt_resets++;
            break;
        case ESP_RST_BROWNOUT:
            g_reset_stats.brownout_resets++;
            break;
        case ESP_RST_DEEPSLEEP:
            g_reset_stats.deepsleep_wakeups++;
            break;
        default:
            g_reset_stats.other_resets++;
            break;
    }
    
    // 保存更新后的统计
    save_stats_to_nvs();
    ESP_LOGI(TAG, "复位统计已更新，原因代码: %d", reason);
}

void print_reset_stats(void) {
    load_stats_from_nvs();
    
    printf("\n════════════════ 复位统计报告 ════════════════\n");
    printf("总重启次数:          %lu\n", (unsigned long)g_reset_stats.total_resets);
    printf("上电复位:            %lu\n", (unsigned long)g_reset_stats.poweron_resets);
    printf("软件复位:            %lu\n", (unsigned long)g_reset_stats.software_resets);
    printf("系统崩溃:            %lu  (需重点关注!)\n", (unsigned long)g_reset_stats.panic_resets);
    printf("任务看门狗复位:      %lu\n", (unsigned long)g_reset_stats.task_wdt_resets);
    printf("中断看门狗复位:      %lu\n", (unsigned long)g_reset_stats.int_wdt_resets);
    printf("掉电复位:            %lu  (检查电源!)\n", (unsigned long)g_reset_stats.brownout_resets);
    printf("深度睡眠唤醒:        %lu\n", (unsigned long)g_reset_stats.deepsleep_wakeups);
    printf("其他复位:            %lu\n", (unsigned long)g_reset_stats.other_resets);
    
    if (g_reset_stats.total_resets > 0) {
        printf("\n各原因占比:\n");
        printf("  正常重启(上电+软件): %.1f%%\n", 
               (g_reset_stats.poweron_resets + g_reset_stats.software_resets) * 100.0 / g_reset_stats.total_resets);
        printf("  异常复位:            %.1f%%\n",
               (g_reset_stats.panic_resets + g_reset_stats.task_wdt_resets + 
                g_reset_stats.int_wdt_resets + g_reset_stats.brownout_resets) * 100.0 / g_reset_stats.total_resets);
        printf("  深度睡眠唤醒:        %.1f%%\n",
               g_reset_stats.deepsleep_wakeups * 100.0 / g_reset_stats.total_resets);
    }
    
    printf("\n最后一次复位:\n");
    printf("  原因代码: %u\n", (unsigned int)g_reset_stats.last_reset_reason);
    
    char time_buf[30];
    struct tm *timeinfo;
    time_t last_time = g_reset_stats.last_reset_time;
    timeinfo = localtime(&last_time);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", timeinfo);
    printf("  时间:     %s\n", time_buf);
    
    printf("═══════════════════════════════════════════════\n\n");
}

const reset_stats_t* get_reset_stats(void) {
    return &g_reset_stats;
}

void clear_reset_stats(void) {
    nvs_handle_t handle;
    
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
        memset(&g_reset_stats, 0, sizeof(g_reset_stats));
        ESP_LOGW(TAG, "已清除所有复位统计");
    }
}

void print_diagnostic_report(void) {
    const char* reset_reason_str = "未知复位原因";
    switch(sys_diag.reset_reason) {
        case ESP_RST_UNKNOWN:       reset_reason_str = "未知原因 (无法确定)"; break;
        case ESP_RST_POWERON:       reset_reason_str = "上电复位 (电源上电/重启)"; break;
        case ESP_RST_EXT:           reset_reason_str = "外部引脚复位 (ESP32保留,通常无效)"; break;
        case ESP_RST_SW:            reset_reason_str = "软件复位 (调用esp_restart())"; break;
        case ESP_RST_PANIC:         reset_reason_str = "系统异常/崩溃 (Software Panic)"; break;
        case ESP_RST_INT_WDT:       reset_reason_str = "中断看门狗超时复位"; break;
        case ESP_RST_TASK_WDT:      reset_reason_str = "任务看门狗超时复位"; break;
        case ESP_RST_WDT:           reset_reason_str = "其他看门狗复位 (泛指)"; break;
        case ESP_RST_DEEPSLEEP:     reset_reason_str = "深度睡眠唤醒复位"; break;
        case ESP_RST_BROWNOUT:      reset_reason_str = "掉电检测复位 (电源电压不稳)"; break;
        case ESP_RST_SDIO:          reset_reason_str = "SDIO外设复位"; break;
        case ESP_RST_USB:           reset_reason_str = "USB外设复位"; break;
        case ESP_RST_JTAG:          reset_reason_str = "JTAG调试复位"; break;
        case ESP_RST_EFUSE:         reset_reason_str = "eFuse错误或CRC校验失败复位"; break;
        case ESP_RST_PWR_GLITCH:    reset_reason_str = "电源毛刺复位"; break;
        case ESP_RST_CPU_LOCKUP:    reset_reason_str = "CPU锁死复位"; break;
        default:                    reset_reason_str = "未定义的复位原因"; break;
    }
    
    const char* wakeup_str = "未知";
    switch(sys_diag.wakeup_cause) {
        case ESP_SLEEP_WAKEUP_EXT0: wakeup_str = "EXT0引脚唤醒"; break;
        case ESP_SLEEP_WAKEUP_EXT1: wakeup_str = "EXT1引脚唤醒"; break;
        case ESP_SLEEP_WAKEUP_ULP: wakeup_str = "ULP协处理器唤醒"; break;
        case ESP_SLEEP_WAKEUP_UNDEFINED: wakeup_str = "未定义唤醒"; break;
        case ESP_SLEEP_WAKEUP_TIMER: wakeup_str = "定时器唤醒"; break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD: wakeup_str = "触摸唤醒"; break;
        case ESP_SLEEP_WAKEUP_GPIO: wakeup_str = "GPIO唤醒"; break;
        case ESP_SLEEP_WAKEUP_UART: wakeup_str = "UART唤醒"; break;
        case ESP_SLEEP_WAKEUP_WIFI: wakeup_str = "WiFi唤醒"; break;
        case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG: wakeup_str = "协处理器陷阱触发"; break;
        case ESP_SLEEP_WAKEUP_BT: wakeup_str = "蓝牙唤醒"; break;
        default: wakeup_str = "冷启动或非睡眠唤醒"; break;
    }
    
    printf("\n===== 系统诊断报告 =====\n");
    printf("复位原因: %s (代码:%d)\n", reset_reason_str, sys_diag.reset_reason);
    printf("唤醒原因: %s (代码:%d)\n", wakeup_str, sys_diag.wakeup_cause);
    
    // 从NVS获取统计信息
    load_stats_from_nvs();
    printf("系统重启总次数: %lu\n", (unsigned long)g_reset_stats.total_resets);
    printf("系统崩溃次数: %lu\n", (unsigned long)g_reset_stats.panic_resets);
    printf("========================\n\n");
}

void system_diagnostics_init(void) {
    ESP_LOGI(TAG, "=== 系统诊断初始化开始 ===");
    
    // 1. 获取诊断信息
    sys_diag.reset_reason = esp_reset_reason();
    sys_diag.wakeup_cause = esp_sleep_get_wakeup_cause();
    
    // 2. 更新复位统计
    update_reset_stats(sys_diag.reset_reason);
    
    // 3. 输出诊断报告
    vTaskDelay(pdMS_TO_TICKS(100));
    print_diagnostic_report();
    
    ESP_LOGI(TAG, "=== 系统诊断初始化完成 ===");
}














