#ifndef RPM_H
#define RPM_H
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include "esp_heap_caps.h"
#include "SW_define.h"
#include "SW_struct.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>

#include <vector>

#include "esp_dsp.h"
#include "dsps_fft2r.h"

extern "C" { // 必须包裹 C 语言头文件
#include "esp_log.h"
}

#define HISTORY_SIZE 10 // 保存最近4个有效周期用于统计
#define MAX_PEAKS 24    // 最大峰值数量
#define MAX_CYCLES 5
#define PEAK_COEFFICIENT 0.7 // 峰值检测系数
#define MIN_SIGNAL_RANGE 4.0  // 最小信号范围

// 必须定义为 >= 256
#define FFT_SIZE 256 

namespace RPM {

    extern SportType m_sportType;                      // 当前运动类型

    struct PeakInfo {
        int index;
        float value;
    };

    static float calculateVariance(float *buffer, int length) {
        // 安全检查
        if (buffer == nullptr || length <= 1) {
            ESP_LOGE("RPM", "Invalid variance calculation params");
            return 0.0f;
        }

        // 辅助 Lambda 函数：计算两个角度之间的最短差值 (target - source)
        // 结果范围锁定在 [-180, 180] 之间
        auto getAngleDiff = [](float target, float source) -> float {
            float diff = target - source;
            // 把差值限制在 -180 到 180 之间
            while (diff > 180.0f)  diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;
            return diff;
        };

        // Welford单次遍历算法
        float mean = buffer[0];
        float M2 = 0.0f;

        for (int i = 1; i < length; i++) {
            float x = buffer[i];

            // 核心修改 1: 计算 x 和 mean 的差时，使用最短夹角
            // 比如 x=170, mean=-165 -> delta 应该是 -25，而不是 335
            float delta = getAngleDiff(x, mean);

            // 更新平均值
            mean += delta / (i + 1);
            
            // 可选：将 mean 归一化到 [-180, 180] 以防止长期漂移（虽然数学上不影响 delta 计算，但为了严谨）
            if (mean > 180.0f) mean -= 360.0f;
            else if (mean < -180.0f) mean += 360.0f;

            // 核心修改 2: 计算 x 和新 mean 的差时，同样使用最短夹角
            float delta2 = getAngleDiff(x, mean);

            M2 += delta * delta2;
        }

        float variance = M2 / length;

        // 防止浮点误差导致的小负数
        return (variance > 0.0f) ? variance : 0.0f;
    }


    class RPM_Compute{
        public:
            float *angle_buffer = nullptr;        // Angle数据缓冲区
            float *_analysisBuffer = nullptr;             

            std::vector<bool> stateVector;        // RPM历史状态栈
            int validHistory[HISTORY_SIZE] = {0}; // 有效周期历史记录
            
            // 原子变量，用于多线程/中断安全
            float avgCycle;          // 移动平均周期值
            float cycleStdDev;       // 周期标准差
            float last_rpm;          // 上一次计算的RPM值
            
            SportType m_sportType = SportType::BYCLING; // 当前运动类型
            bool is_halving_mode = false;               // 是否为减半模式
            int min_signal_range = 10;                  // 最小信号范围

    private:
            float *_fft_work_buf;
            // 只需要存 256 个 float (足够涵盖 FFT_SIZE)
            float _history_buffer[256] = {0};  
            int _history_len = 0;       // 记录上一帧数据的实际长度

    public:
        RPM_Compute() {
            // 1. 动态内存分配 (放在构造函数中更安全)
            angle_buffer = (float *)heap_caps_aligned_alloc(ALIGNMENT_SIZE, rpm_buffer_block, MALLOC_CAP_8BIT);
            
            // 检查内存是否分配成功
            if (angle_buffer == nullptr) {
                // 处理内存分配失败的情况，例如打印错误日志
                // ESP_LOGE("RPM", "Failed to allocate angle_buffer");
            } else {
                // 分配成功，清零内存
                memset(angle_buffer, 0, rpm_buffer_block);
            }

            _analysisBuffer = (float *)malloc(rpm_buffer_block); // 一次性分配
            if (!_analysisBuffer) {}

            // 2. 初始化原子变量
            avgCycle = 0.0f;
            cycleStdDev = 0.0f;
            last_rpm = 0.0f;

            // 3. 初始化数组
            memset(validHistory, 0, sizeof(validHistory));
            
            // 4. 预留 vector 空间 (避免频繁重分配)
            stateVector.reserve(HISTORY_SIZE * 2); 

            // 1. 确保 FFT 库已全局初始化
            // 即使创建多个对象，多次调用这个 init 也是安全的(库内部有检查)，或者你可以加个静态锁
            dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);

            _fft_work_buf = (float*)heap_caps_aligned_alloc(16, FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

            // 检查申请是否成功
            if (_fft_work_buf == NULL) {
                printf("Error: Failed to allocate aligned memory for FFT!\n");
            }
        }

        ~RPM_Compute() {
            // 释放动态分配的内存，防止内存泄漏
            if (angle_buffer != nullptr) {
                free(angle_buffer); 
                // 或者使用 heap_caps_free(angle_buffer);
                angle_buffer = nullptr;
            }
            if (_analysisBuffer != nullptr) {
                free(_analysisBuffer);
                _analysisBuffer = nullptr;
            }
            if (_fft_work_buf) {
                heap_caps_free(_fft_work_buf); // 释放对齐内存
            }
        }

        // RPM历史状态栈,不为0则为True,为0则为False
        bool isValidStateStack();
        // 输入RPM
        // 输出RPM队列下一次指向的索引值
        void dealRPMVector(float rpm,int& index,bool isFull = true);
        // RPM 计算相关函数
        float calculateRPM(bool isFull = true,int len = WINDOW_BUFFER_SIZE,SportType sportType = SportType::BYCLING);
        // 使用 ESP-DSP 进行 FFT 低通滤波
        void performFFTSmoothingIDF(float *data, int len, float fft_keep_ratio);
        // 移除趋势的移动平均
        std::vector<float> removeTrendMovingAverage(const float *data, int length,
                                              int window_size = WINDOW_BUFFER_SIZE);
        // 周期检测相关函数
        int findSignificantPeaks(const float *data, int len, PeakInfo *peaks);
        // 检测并返回稳定周期
        int detectCyclePeriod(bool isFull = true,int len = WINDOW_BUFFER_SIZE);

        // 选择最稳定的周期（基于中位数滤波）
        int selectBestCycle(const int *candidates, int count);
        // 间隔有效性验证函数
        bool isValidInterval(int interval_ms);
    };
}
#endif