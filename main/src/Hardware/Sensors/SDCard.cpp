#include <string>
#include <vector>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_timer.h"
//#include "tinyusb.h"
#include "esp_err.h"
#include "SDCard.h"

//#include "class/msc/msc_device.h"

#include "device/usbd.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"


#include <sys/stat.h>
#include <utime.h>
#include <time.h>
#include <diskio.h>

// 添加互斥锁定义（保护环形缓冲区和文件操作）
#include "freertos/semphr.h"

// #include "USB.h"
// #include "FirmwareMSC.h"

// #if !ARDUINO_USB_MSC_ON_BOOT
// FirmwareMSC MSC_Update;
// #endif

sdmmc_card_t* card = nullptr;

bool  sd_card_initialized = false;

// 全局变量 
FILE* data_file = nullptr;
int64_t last_write_time = 0;
int record_count = 0;
int current_file_size = 0;
int file_counter = 0;

// static void usbEventCallback(void *arg,
//                              esp_event_base_t event_base,
//                              int32_t event_id,
//                              void *event_data) {
//   if (event_base == ARDUINO_USB_EVENTS) {
//     arduino_usb_event_data_t *data = (arduino_usb_event_data_t *)event_data;
//     switch (event_id) {
//       case ARDUINO_USB_STARTED_EVENT:
//         Serial.println("USB PLUGGED");
//         break;
//       case ARDUINO_USB_STOPPED_EVENT:
//         Serial.println("USB UNPLUGGED");
//         break;
//       case ARDUINO_USB_SUSPEND_EVENT:
//         Serial.printf("USB SUSPENDED: remote_wakeup_en: %u\n",
//                       data->suspend.remote_wakeup_en);
//         break;
//       case ARDUINO_USB_RESUME_EVENT:
//         Serial.println("USB RESUMED");
//         break;
//       default:
//         break;
//     }
//   } else if (event_base == ARDUINO_FIRMWARE_MSC_EVENTS) {
//     arduino_firmware_msc_event_data_t *data =
//         (arduino_firmware_msc_event_data_t *)event_data;
//     switch (event_id) {
//       case ARDUINO_FIRMWARE_MSC_START_EVENT:
//         Serial.println("MSC Update Start");
//         break;
//       case ARDUINO_FIRMWARE_MSC_WRITE_EVENT:
//         Serial.print(".");
//         break;
//       case ARDUINO_FIRMWARE_MSC_END_EVENT:
//         Serial.printf("\nMSC Update End: %u bytes\n", data->end.size);
//         break;
//       case ARDUINO_FIRMWARE_MSC_ERROR_EVENT:
//         Serial.printf("MSC Update ERROR! Progress: %u bytes\n",
//                       data->error.size);
//         break;
//       case ARDUINO_FIRMWARE_MSC_POWER_EVENT:
//         Serial.printf("MSC Update Power: power: %u, start: %u, eject: %u\n",
//                       data->power.power_condition,
//                       data->power.start,
//                       data->power.load_eject);
//         break;
//       default:
//         break;
//     }
//   }
// }

// 环形缓冲区
char write_buffer[WRITE_BUFFER_SIZE][MAX_LINE_LENGTH];
int buffer_head = 0;
int buffer_tail = 0;
int buffer_count = 0;

// 存储事件描述
const char *storage_event_str[] = {
    "Mount Start",
    "Mount Complete", 
    "Mount Failed",
    "Format Required",
};

// 获取当前时间字符串（格式：YYYY-MM-DD HH:MM:SS.sss 或 Unix时间戳）
void get_current_time_str(char* time_str, size_t max_len) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        // 有本地时间时：YYYY-MM-DD HH:MM:SS.sss
        int64_t now_us = esp_timer_get_time();
        int milliseconds = (now_us / 1000) % 1000;  // 提取毫秒部分
        
        strftime(time_str, max_len, "%Y-%m-%d %H:%M:%S", &timeinfo);
        // 追加毫秒（确保不越界）
        if (strlen(time_str) + 5 < max_len) {  // +5 对应 ".xxx"
            snprintf(time_str + strlen(time_str), max_len - strlen(time_str), ".%03d", milliseconds);
        }
    } else {
        // 无本地时间时：Unix时间戳（秒.微秒）
        int64_t now_us = esp_timer_get_time();
        snprintf(time_str, max_len, "%lld.%06lld", now_us / 1000000, now_us % 1000000);
    }
}

// 初始化SD卡
bool init_sd_card() {
    if (sd_card_initialized) {
        Serial.println("SD card already initialized");
        return false;
    }

    esp_err_t ret;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = MAX_FILES + 2,  // 额外空间用于系统文件
        .allocation_unit_size = 16 * 1024
    };
    
    // SD卡主机配置
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    #if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
    // sd_pwr_ctrl_ldo_config_t ldo_config = {
    //     .ldo_chan_id = CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_IO_ID,
    // };
    // sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

    // ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    // if (ret != ESP_OK) {
    //     printf("Failed to create a new on-chip LDO power control driver");
    //     return;
    // }
    // host.pwr_ctrl_handle = pwr_ctrl_handle;
    #endif
    // SD卡槽配置
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;

    slot_config.clk = GPIO_NUM_47;
    slot_config.cmd = GPIO_NUM_48;
    slot_config.d0 = GPIO_NUM_21;

    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // 挂载文件系统
    const char mount_point[] = MOUNT_POINT;
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            printf("Failed to mount filesystem. "
                     "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            printf("Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));   
        }
        return false;
    }
    printf("Filesystem mounted");
    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    // 创建第一个数据文件
    char first_file[256];
    char timestamp_str[64];
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        // 使用strftime格式化时间部分
        strftime(timestamp_str, sizeof(timestamp_str), "%m%d%H%M", &timeinfo);            // %Y%m%d%H%M
        snprintf(first_file, sizeof(first_file), MOUNT_POINT"/%s.csv", timestamp_str);
    }
    else {
        snprintf(first_file, sizeof(first_file), MOUNT_POINT"/data_%d.csv", file_counter);
    }

    data_file = fopen(first_file, "w");
    if (!data_file) {
        printf("Failed to create file: %s\n", first_file);
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        return false;
    }

    // 写入CSV表头
    const char* header = "roll,pitch,yaw,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,"
                         "vert_line_accel,accel_mag,hor_accel,vert_percent,quat_x,quat_y,quat_z,quat_w,rpm,type,time\n";
    fprintf(data_file, "%s", header);
    fflush(data_file);
    
    // 设置文件创建时间为当前校准时间
    set_file_creation_time(first_file);

    // 初始化文件大小跟踪
    current_file_size = strlen(header);
    last_write_time = esp_timer_get_time();
    printf("SD initialized. File: %s\n", first_file);

    sd_card_initialized = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    return true;
}

// 设置文件的创建和修改时间
bool set_file_creation_time(const char* filepath) {
    struct tm timeinfo;
    
    // 获取当前校准的系统时间
    if (!getLocalTime(&timeinfo)) {
        Serial.println("[FILE TIME] Failed to get current time");
        return false;
    }
    
    // 将tm结构转换为time_t
    time_t file_time = mktime(&timeinfo);
    
    // 设置文件访问和修改时间
    struct utimbuf new_times;
    new_times.actime = file_time;   // 访问时间
    new_times.modtime = file_time;  // 修改时间
    
    if (utime(filepath, &new_times) == 0) {
        // 打印设置的时间信息
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
        printf("[FILE TIME] Set file time to: %s\n", time_str);
        return true;
    } else {
        Serial.println("[FILE TIME] Failed to set file time");
        return false;
    }
}

bool create_new_file()
{
    // 创建第一个数据文件
    char first_file[256];
    char timestamp_str[64];
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        // 使用strftime格式化时间部分
        strftime(timestamp_str, sizeof(timestamp_str), "%m%d%H%M", &timeinfo);  // 
        snprintf(first_file, sizeof(first_file), MOUNT_POINT"/%s.csv", timestamp_str);
    }
    else {
        snprintf(first_file, sizeof(first_file), MOUNT_POINT"/data_%d.csv", file_counter);
    }
    data_file = fopen(first_file, "w");
    if (!data_file) {
        printf("Failed to create file: %s\n", first_file);
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        return false;
    }

    // 写入CSV表头
    const char* header = "roll,pitch,yaw,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,"
                         "vert_line_accel,accel_mag,hor_accel,vert_percent,quat_x,quat_y,quat_z,quat_w,rpm,type,time\n";
    fprintf(data_file, "%s", header);
    fflush(data_file);

    // 设置文件创建时间为当前校准时间
    set_file_creation_time(first_file);
    
    // 初始化文件大小跟踪
    current_file_size = strlen(header);
    last_write_time = esp_timer_get_time();
    // printf("SD initialized. File: %s\n", first_file);

    vTaskDelay(pdMS_TO_TICKS(100));
    return true;
}

bool deinit_sd_card() {
    if (!sd_card_initialized) {
        Serial.println("SD Card already deinitialized");
        return false;
    }

    esp_err_t ret;
    bool success = true;
    
    printf("Starting SD card unmount process...\n");
    
    // 1. 关闭所有打开的文件
    if (data_file != NULL) {
        if (fclose(data_file) == 0) {
            printf("Data file closed successfully\n");
            data_file = NULL;
        } else {
            printf("Warning: Failed to close data file properly\n");
            success = false;
        }
    }
    
    // 2. 卸载文件系统
    if (card != NULL) {
        ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        if (ret != ESP_OK) {
            printf("Failed to unmount SD card (%s)\n", esp_err_to_name(ret));
            success = false;
        } else {
            printf("Filesystem unmounted successfully\n");
        }

        // 3. 释放电源控制资源
        #if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
        // if (card->host.pwr_ctrl_handle != NULL) {
        //     esp_err_t pwr_ret = sd_pwr_ctrl_del_handle(card->host.pwr_ctrl_handle);
        //     if (pwr_ret != ESP_OK) {
        //         printf("Warning: Failed to delete power control handle (%s)\n", 
        //                     esp_err_to_name(pwr_ret));
        //         success = false;
        //     } else {
        //         printf("Power control handle released\n");
        //     }
        // }
        #endif
        
        card = NULL;
    } else {
        printf("No SD card instance found\n");
    }
    
    // 5. 重置状态变量
    current_file_size = 0;
    // 注意：这里不重置 file_counter，以便下次挂载时创建新文件
    
    if (success) {
        printf("SD card deinitialization completed successfully\n");
    } else {
        printf("SD card deinitialization completed with warnings\n");
    }

    file_counter++;
    sd_card_initialized = false;
    return success;
}

// 添加数据到缓冲区
void add_to_buffer(const FusionPacket& packet) {
    // 检查是否需要切换文件
    if (current_file_size >= MAX_FILE_SIZE) {
        write_to_sd(); // 先写入剩余数据
    //printf("Write to sd , File size: %d bytes\n", current_file_size);
        rotate_file(); // 创建新文件
    }

    char time_str[32];  // 存储时间字符串
    get_current_time_str(time_str, sizeof(time_str));  // 获取当前时间

    // 格式化数据行
    char line[MAX_LINE_LENGTH];
    int len = snprintf(line, sizeof(line),
        "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.6f,%.6f,%.6f,%.6f,%.1f,%d,%s\n",
        packet.roll, packet.pitch, packet.yaw,
        packet.accel_x, packet.accel_y, packet.accel_z,
        packet.gyro_x, packet.gyro_y, packet.gyro_z,
        packet.vert_line_accel, packet.accel_mag, packet.hor_accel, packet.vert_percent,
        packet.quat_x, packet.quat_y, packet.quat_z, packet.quat_w,
        packet.rpm,packet.type,time_str);
    
    // 缓冲区满时强制写入
    if (buffer_count >= WRITE_BUFFER_SIZE) {
        write_to_sd();
    }
    
    // 添加到环形缓冲区
    strncpy(write_buffer[buffer_head], line, MAX_LINE_LENGTH);
    buffer_head = (buffer_head + 1) % WRITE_BUFFER_SIZE;
    buffer_count++;
    
    // 定时检查
    int64_t now = esp_timer_get_time();
    if ((now - last_write_time) >= WRITE_INTERVAL_US && buffer_count > 0) {
        write_to_sd();
        last_write_time = now;
    }
}

// 写入数据到SD卡
void write_to_sd() {
    if (buffer_count == 0 || !data_file) {
        return;
    }

    size_t bytes_written = 0;
    size_t records_to_write = buffer_count;
    
    for (size_t i = 0; i < records_to_write; i++) {
        // 计算实际写入长度
        size_t len = strlen(write_buffer[buffer_tail]);
        bytes_written += len;
        
        // 写入文件
        if (fprintf(data_file, "%s", write_buffer[buffer_tail]) < 0) {
            Serial.println("Write error!");
            break;
        }
        
        // 更新缓冲区指针
        buffer_tail = (buffer_tail + 1) % WRITE_BUFFER_SIZE;
        buffer_count--;
        record_count++;
    }
    
    // 更新文件大小
    current_file_size += bytes_written;
    
    // 立即刷新确保数据持久化
    fflush(data_file);
    // printf("Wrote %d records. File size: %d bytes\n", 
    //              records_to_write, current_file_size);

}

// 切换文件
void rotate_file() {
    if (file_counter >= MAX_FILES) {
        Serial.println("Max files reached!");
        return;
    }
    
    // 关闭当前文件
    if (data_file) {
        fclose(data_file);
        data_file = nullptr;
    }
    
    // 创建新文件
    file_counter++;
    char new_file[256];
    char timestamp_str[64];
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        // 使用strftime格式化时间部分
        strftime(timestamp_str, sizeof(timestamp_str), "%m%d%H%M", &timeinfo);
        snprintf(new_file, sizeof(new_file), MOUNT_POINT"/%s.csv", timestamp_str);
    }
    else {
        snprintf(new_file, sizeof(new_file), MOUNT_POINT"/data_%d.csv", file_counter);
    }
    
    data_file = fopen(new_file, "w");
    if (!data_file) {
        printf("Failed to create: %s\n", new_file);
        return;
    }
    
    // 写入表头
    const char* header = "roll,pitch,yaw,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,"
                         "vert_line_accel,accel_mag,hor_accel,vert_percent,quat_x,quat_y,quat_z,quat_w,rpm,type,time\n";
    fprintf(data_file, "%s", header);
    fflush(data_file);
    // 关键修复：立即写入可能存在的缓冲数据
    if (buffer_count > 0) {
        write_to_sd(); // 将积压数据写入新文件
    }
    
    // 设置文件创建时间为当前校准时间
    set_file_creation_time(new_file);

    // 重置文件大小跟踪
    current_file_size = strlen(header);
    printf("New file created: %s\n", new_file);
}

// 清理资源
void cleanup_sd_card() {
    write_to_sd(); // 写入剩余数据
    
    if (data_file) {
        fclose(data_file);
        data_file = nullptr;
    }
    
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    Serial.println("SD card unmounted");

}

// 定时检查函数
void check_sd_write() {
    int64_t now = esp_timer_get_time();
    if ((now - last_write_time) >= WRITE_INTERVAL_US && buffer_count > 0) {
        write_to_sd();
        last_write_time = now;
    }
}

// #include "SDCard.h"
// #include "esp_err.h"
// #include "freertos/semphr.h"
// #include <sys/stat.h>
// #include <utime.h>
// #include <time.h>
// #include <diskio.h>
// #include <stdio.h>
// #include <string.h>
// #include <Arduino.h>  // 确保 Serial/getLocalTime 可用

// // 全局变量初始化
// bool sd_card_initialized = false;
// int file_counter = 0;
// sdmmc_card_t* card = nullptr;

// // 全局状态变量
// FILE* data_file = nullptr;
// int64_t last_write_time = 0;
// int record_count = 0;
// int current_file_size = 0;
// SemaphoreHandle_t sd_mutex = nullptr;  // 互斥锁（新增）

// // 环形缓冲区
// char write_buffer[WRITE_BUFFER_SIZE][MAX_LINE_LENGTH];
// int buffer_head = 0;
// int buffer_tail = 0;
// int buffer_count = 0;

// // 存储事件描述（未使用，保留原定义）
// const char *storage_event_str[] = {
//     "Mount Start", "Mount Complete", "Mount Failed", "Format Required"
// };

// // ================== 工具函数 ==================
// // 创建新文件名（抽取为独立函数）
// std::string create_filename() {
//     char filename[256];
//     char timestamp_str[64];
//     struct tm timeinfo;

//     if (getLocalTime(&timeinfo)) {
//         // 修复：添加年份和秒，避免文件名重复（原 "%m%d%H%M"）
//         strftime(timestamp_str, sizeof(timestamp_str), "%Y%m%d_%H%M%S", &timeinfo);
//         snprintf(filename, sizeof(filename), "%s/%s.csv", MOUNT_POINT, timestamp_str);
//     } else {
//         snprintf(filename, sizeof(filename), "%s/data_%d.csv", MOUNT_POINT, file_counter);
//     }
//     return std::string(filename);
// }

// // 设置文件创建/修改时间
// bool set_file_creation_time(const char* filepath) {
//     struct tm timeinfo;
//     if (!getLocalTime(&timeinfo)) {
//         Serial.println("[FILE TIME] Failed to get current time");
//         return false;
//     }

//     time_t file_time = mktime(&timeinfo);
//     struct utimbuf new_times = {.actime = file_time, .modtime = file_time};
    
//     if (utime(filepath, &new_times) == 0) {
//         char time_str[32];
//         strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
//         printf("[FILE TIME] Set file time to: %s\n", time_str);
//         return true;
//     } else {
//         Serial.println("[FILE TIME] Failed to set file time");
//         return false;
//     }
// }

// // 创建新文件（内部调用，需已持锁）
// static bool create_new_file_locked() {
//     // 关闭旧文件（若存在）
//     if (data_file) {
//         fclose(data_file);
//         data_file = nullptr;
//     }

//     std::string filename = create_filename();
//     data_file = fopen(filename.c_str(), "w");
//     if (!data_file) {
//         Serial.printf("Failed to create file: %s\n", filename.c_str());
//         return false;
//     }

//     // 写入表头并更新文件大小（修复：用 fprintf 返回值计算实际写入长度）
//     const char* header = "roll,pitch,yaw,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,"
//                          "vert_line_accel,accel_mag,hor_accel,vert_percent,quat_x,quat_y,quat_z,quat_w,rpm,type\n";
//     int header_len = fprintf(data_file, "%s", header);
//     fflush(data_file);

//     if (header_len < 0) {
//         Serial.printf("Failed to write header: %s\n", filename.c_str());
//         fclose(data_file);
//         data_file = nullptr;
//         return false;
//     }

//     current_file_size = header_len;  // 修复：用实际写入长度代替 strlen
//     last_write_time = esp_timer_get_time();
//     set_file_creation_time(filename.c_str());
//     Serial.printf("New file created: %s\n", filename.c_str());
//     return true;
// }

// // ================== 公开函数 ==================
// // 初始化SD卡
// bool init_sd_card() {
//     if (sd_card_initialized) {
//         Serial.println("SD card already initialized");
//         return true;  // 修复：已初始化应返回 true（原返回 false）
//     }

//     // 初始化互斥锁（新增）
//     if (!sd_mutex) {
//         sd_mutex = xSemaphoreCreateMutex();
//         if (!sd_mutex) {
//             Serial.println("Failed to create SD mutex");
//             return false;
//         }
//     }

//     esp_err_t ret;
//     esp_vfs_fat_sdmmc_mount_config_t mount_config = {
//         .format_if_mount_failed = false,
//         .max_files = MAX_FILES + 2,  // 额外空间用于系统文件
//         .allocation_unit_size = 16 * 1024
//     };

//     // SD卡主机配置
//     sdmmc_host_t host = SDMMC_HOST_DEFAULT();
//     sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
//     slot_config.width = 1;  // 单线模式（SPI）

//     // 引脚配置（根据硬件调整，原 GPIO47/48/21）
//     slot_config.clk = GPIO_NUM_47;
//     slot_config.cmd = GPIO_NUM_48;
//     slot_config.d0 = GPIO_NUM_21;
//     slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

//     // 挂载文件系统
//     ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
//     if (ret != ESP_OK) {
//         Serial.printf("SD mount failed: %s\n", esp_err_to_name(ret));
//         return false;
//     }
//     sdmmc_card_print_info(stdout, card);

//     // 创建第一个文件（加锁保护）
//     xSemaphoreTake(sd_mutex, portMAX_DELAY);
//     buffer_head = buffer_tail = buffer_count = 0;  // 重置缓冲区
//     bool success = create_new_file_locked();
//     xSemaphoreGive(sd_mutex);

//     if (success) {
//         sd_card_initialized = true;
//         Serial.println("SD card initialized successfully");
//     } else {
//         esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
//         card = nullptr;
//     }
//     return success;
// }

// // 反初始化SD卡
// bool deinit_sd_card() {
//     if (!sd_card_initialized) {
//         Serial.println("SD Card not initialized");
//         return true;  // 修复：未初始化应返回 true（原返回 false）
//     }

//     xSemaphoreTake(sd_mutex, portMAX_DELAY);
//     // 关闭文件（若存在）
//     if (data_file) {
//         fclose(data_file);
//         data_file = nullptr;
//     }
//     xSemaphoreGive(sd_mutex);

//     // 卸载文件系统
//     if (card) {
//         esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
//         if (ret != ESP_OK) {
//             Serial.printf("Unmount failed: %s\n", esp_err_to_name(ret));
//             return false;
//         }
//         card = nullptr;
//     }

//     // 释放互斥锁（新增）
//     if (sd_mutex) {
//         vSemaphoreDelete(sd_mutex);
//         sd_mutex = nullptr;
//     }

//     sd_card_initialized = false;
//     file_counter++;  // 确保下次挂载创建新文件序列
//     Serial.println("SD card deinitialized");
//     return true;
// }

// // 添加数据到缓冲区（核心修复：线程安全+溢出保护）
// void add_to_buffer(const FusionPacket& packet) {
//     if (!sd_card_initialized || !sd_mutex) return;

//     xSemaphoreTake(sd_mutex, portMAX_DELAY);

//     // 检查文件大小，超过则切换文件（原逻辑保留，加锁保护）
//     if (current_file_size >= MAX_FILE_SIZE) {
//         write_to_sd();  // 先写入缓冲区数据
//         rotate_file();  // 创建新文件
//     }

//     // 格式化数据行（修复：限制长度，避免缓冲区溢出）
//     char line[MAX_LINE_LENGTH];
//     int len = snprintf(line, sizeof(line)-1,  // 预留终止符空间
//         "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.6f,%.6f,%.6f,%.6f,%.1f,%d\n",
//         packet.roll, packet.pitch, packet.yaw,
//         packet.accel_x, packet.accel_y, packet.accel_z,
//         packet.gyro_x, packet.gyro_y, packet.gyro_z,
//         packet.vert_line_accel, packet.accel_mag, packet.hor_accel, packet.vert_percent,
//         packet.quat_x, packet.quat_y, packet.quat_z, packet.quat_w,
//         packet.rpm, packet.type);
//     line[MAX_LINE_LENGTH-1] = '\0';  // 确保终止符

//     // 缓冲区满时强制写入（原逻辑保留，加锁保护）
//     if (buffer_count >= WRITE_BUFFER_SIZE) {
//         write_to_sd();
//     }

//     // 添加到环形缓冲区
//     strncpy(write_buffer[buffer_head], line, MAX_LINE_LENGTH);
//     buffer_head = (buffer_head + 1) % WRITE_BUFFER_SIZE;
//     buffer_count++;

//     // 定时写入检查
//     int64_t now = esp_timer_get_time();
//     if ((now - last_write_time) >= WRITE_INTERVAL_US && buffer_count > 0) {
//         write_to_sd();
//         last_write_time = now;
//     }

//     xSemaphoreGive(sd_mutex);
// }

// // 写入缓冲区数据到SD卡（加锁保护）
// void write_to_sd() {
//     if (!sd_card_initialized || !sd_mutex || buffer_count == 0 || !data_file) return;

//     xSemaphoreTake(sd_mutex, portMAX_DELAY);

//     size_t bytes_written = 0;
//     int records_to_write = buffer_count;
//     for (int i = 0; i < records_to_write; i++) {
//         size_t line_len = strlen(write_buffer[buffer_tail]);
//         if (fprintf(data_file, "%s", write_buffer[buffer_tail]) < 0) {
//             Serial.println("Write error during buffer flush");
//             break;
//         }
//         bytes_written += line_len;
//         buffer_tail = (buffer_tail + 1) % WRITE_BUFFER_SIZE;
//         buffer_count--;
//         record_count++;
//     }

//     current_file_size += bytes_written;  // 更新文件大小
//     fflush(data_file);  // 强制刷新到磁盘
//     Serial.printf("Wrote %d records, file size: %d bytes\n", records_to_write, current_file_size);

//     xSemaphoreGive(sd_mutex);
// }

// // 切换文件（加锁保护）
// void rotate_file() {
//     if (!sd_card_initialized || !sd_mutex) return;

//     xSemaphoreTake(sd_mutex, portMAX_DELAY);

//     if (file_counter >= MAX_FILES) {
//         Serial.println("Max files reached!");
//         xSemaphoreGive(sd_mutex);
//         return;
//     }

//     file_counter++;
//     bool success = create_new_file_locked();  // 复用创建文件逻辑

//     xSemaphoreGive(sd_mutex);
//     if (!success) Serial.println("Failed to rotate file");
// }

// // 清理资源（统一接口）
// void cleanup_sd_card() {
//     write_to_sd();  // 写入剩余数据
//     deinit_sd_card();  // 调用反初始化函数
// }

// // 定时检查写入（外部任务调用）
// void check_sd_write() {
//     if (!sd_card_initialized || !sd_mutex) return;

//     xSemaphoreTake(sd_mutex, portMAX_DELAY);
//     int64_t now = esp_timer_get_time();
//     if ((now - last_write_time) >= WRITE_INTERVAL_US && buffer_count > 0) {
//         write_to_sd();  // 直接调用写入（已持锁）
//         last_write_time = now;
//     }
//     xSemaphoreGive(sd_mutex);
// }

// // 原 create_new_file 函数（保留，但建议优先使用 create_new_file_locked）
// bool create_new_file() {
//     if (!sd_card_initialized || !sd_mutex) return false;
//     xSemaphoreTake(sd_mutex, portMAX_DELAY);
//     bool success = create_new_file_locked();
//     xSemaphoreGive(sd_mutex);
//     return success;
// }