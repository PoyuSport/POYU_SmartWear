#include <string>
#include <vector>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_timer.h"
#include "SW_struct.h"

#include "sd_protocol_types.h"

//#include "tinyusb.h"

static const char *TAG = "usb_msc_sd_card";
extern bool sd_card_initialize;

// 配置参数
constexpr int WRITE_BUFFER_SIZE = 300;      // 缓冲区大小（1秒数据量）
constexpr int64_t MAX_FILE_SIZE = 1 * 1024 * 1024;  // 1MB文件大小
constexpr int64_t WRITE_INTERVAL_US = 1000000; // 1秒写入间隔
constexpr int MAX_FILES = 1*200;                 // 支持最大1GB
constexpr int MAX_LINE_LENGTH = 256;       // 单行最大长度

extern int file_counter;

extern sdmmc_card_t* card;

#define MOUNT_POINT "/sdcard"

// 创建新文件名
std::string create_filename();

// 初始化SD卡并创建第一个文件
bool init_sd_card();

bool deinit_sd_card(void);

// 设置文件的创建和修改时间
bool set_file_creation_time(const char *filepath);

bool create_new_file();

// 将缓冲区数据写入SD卡
void write_to_sd();

// 将数据包添加到写入缓冲区
void add_to_buffer(const FusionPacket &packet);

void rotate_file();

// 在程序结束时清理资源
void cleanup_sd_card();

// 定期调用此函数确保数据及时写入
void check_sd_write();

// // 初始化USB MSC功能
// void init_usb_msc();

// #ifndef SDCARD_H
// #define SDCARD_H

// #include <string>
// #include <vector>
// #include "esp_vfs_fat.h"
// #include "sdmmc_cmd.h"
// #include "driver/sdmmc_host.h"
// #include "esp_timer.h"
// #include "SW_struct.h"  // 包含 FusionPacket 定义
// #include "sd_protocol_types.h"

// static const char *TAG = "usb_msc_sd_card";
// extern bool sd_card_initialized;  // 修改：统一变量名（原 sd_card_initialize）

// // 配置参数（与源文件保持一致）
// constexpr int WRITE_BUFFER_SIZE = 300;        // 缓冲区大小（1秒数据量）
// constexpr int64_t MAX_FILE_SIZE = 1 * 1024 * 1024;  // 1MB文件大小
// constexpr int64_t WRITE_INTERVAL_US = 1000000; // 1秒写入间隔
// constexpr int MAX_FILES = 200;                 // 支持最大200个文件（原 1*200）
// constexpr int MAX_LINE_LENGTH = 256;          // 单行最大长度

// extern int file_counter;
// extern sdmmc_card_t* card;

// #define MOUNT_POINT "/sdcard"

// // 函数声明
// std::string create_filename();
// bool init_sd_card();
// bool deinit_sd_card(void);
// bool set_file_creation_time(const char *filepath);
// bool create_new_file();
// void write_to_sd();
// void add_to_buffer(const FusionPacket &packet);
// void rotate_file();
// void cleanup_sd_card();
// void check_sd_write();

// #endif // SDCARD_H