#ifndef SYSTEM_DIAGNOSTICS_H
#define SYSTEM_DIAGNOSTICS_H

#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// 系统诊断信息结构体
struct SystemDiagnostics {
    esp_reset_reason_t reset_reason;
    esp_sleep_wakeup_cause_t wakeup_cause;
    uint32_t restart_count;
    uint32_t panic_count;
};

// 复位统计结构体
typedef struct {
    uint32_t total_resets;
    uint32_t poweron_resets;
    uint32_t software_resets;
    uint32_t panic_resets;
    uint32_t task_wdt_resets;
    uint32_t int_wdt_resets;
    uint32_t brownout_resets;
    uint32_t deepsleep_wakeups;
    uint32_t other_resets;
    uint32_t last_reset_reason;
    uint32_t last_reset_time;
} reset_stats_t;

// ===== 诊断系统函数 =====
void system_diagnostics_init(void);
void update_reset_stats(esp_reset_reason_t reason);
void print_reset_stats(void);
const reset_stats_t* get_reset_stats(void);
void clear_reset_stats(void);
void print_diagnostic_report(void);

// 全局变量声明
extern SystemDiagnostics sys_diag;

#endif // SYSTEM_DIAGNOSTICS_H