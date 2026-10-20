#include <RPM.h>
#include <algorithm> 
#include <functional>  
#include <SW_define.h>
#include <BLE.h>
#include <vector>
#include "esp_dsp.h"
#include "dsps_fft2r.h"

namespace RPM {

    bool RPM_Compute::isValidStateStack(){
        std::vector<bool> vector = RPM::RPM_Compute::stateVector;
        while(!vector.empty())
        {
            bool state = vector.at(vector.size() - 1); // 获取最后一个向量的元素
            if(state == true)
            {
                return true; // 只要有一个有效周期就返回true
            }
            vector.pop_back();
        }
        return false;
    }

    // 数值稳定但延迟比较大，但数值非常稳定的版本 V1
    void RPM_Compute::dealRPMVector(float rpm,int& index,bool isFull){
        static int times =
            WINDOW_BUFFER_SIZE / INFERENCE_DATA_NUM;

        if(rpm > 0.001f && isFull)
        {
            for(int t_index = WINDOW_BUFFER_SIZE /times; t_index < WINDOW_BUFFER_SIZE; t_index++){
                RPM::RPM_Compute::angle_buffer[t_index - WINDOW_BUFFER_SIZE /times] = RPM::RPM_Compute::angle_buffer[t_index];
            }
            memset(RPM::RPM_Compute::angle_buffer + WINDOW_BUFFER_SIZE - WINDOW_BUFFER_SIZE / times, 0, WINDOW_BUFFER_SIZE / times * sizeof(float)); // 清空后面的数据
            index = WINDOW_BUFFER_SIZE - WINDOW_BUFFER_SIZE / times; // 更新索引
        }
        RPM::RPM_Compute::last_rpm = rpm > 0.001f ? rpm : 0.0f; // 确保小于阈值时为0
        // RPM为每分钟圈数，60为一分钟有60秒，相除则为速度，而RPM是每1秒计算一次，所以乘以1
        BLE::g_sportCount.store(BLE::g_sportCount.load() + rpm/60.0*1.0);
    }

      // 开始运动时延迟相对好点，但数值变化范围大  
    // void RPM_Compute::dealRPMVector(float rpm,int& index,bool isFull){
    //     static int times =
    //         WINDOW_BUFFER_SIZE / INFERENCE_DATA_NUM;

    //     stateVector.push_back(rpm > 0.001f ? true : false); // 添加当前状态 
    //     if(stateVector.size() == times){
    //         if(isValidStateStack()){
    //             if(rpm > 0.001f && isFull)
    //             {
    //                 for(int t_index = WINDOW_BUFFER_SIZE / times; t_index < WINDOW_BUFFER_SIZE; t_index++){
    //                     RPM::angle_buffer[t_index - WINDOW_BUFFER_SIZE / times] = RPM::angle_buffer[t_index];
    //                 }
    //                 memset(RPM::angle_buffer + WINDOW_BUFFER_SIZE - WINDOW_BUFFER_SIZE / times, 0, WINDOW_BUFFER_SIZE / times * sizeof(float)); // 清空后面的数据
    //                 index = WINDOW_BUFFER_SIZE - WINDOW_BUFFER_SIZE / times; // 更新索引

    //                 stateVector.erase(stateVector.begin()); 
    //             }
    //         }
    //         else{
    //             if(rpm > 0.001f && isFull)
    //             {
    //                 //如果栈满了，删除先前的状态
    //                 for(int t_index =WINDOW_BUFFER_SIZE - WINDOW_BUFFER_SIZE /times; t_index < WINDOW_BUFFER_SIZE; t_index++){
    //                     RPM::angle_buffer[t_index - WINDOW_BUFFER_SIZE + WINDOW_BUFFER_SIZE /times] = RPM::angle_buffer[t_index];
    //                 }
    //                 memset(RPM::angle_buffer + WINDOW_BUFFER_SIZE /times, 0, (WINDOW_BUFFER_SIZE - WINDOW_BUFFER_SIZE / times)* sizeof(float)); // 清空后面的数据
    //                 index = WINDOW_BUFFER_SIZE / times; // 更新索引

    //                 // 保留最后一条数据
    //                 while((!stateVector.empty()) && (stateVector.size() != 1)){
    //                     stateVector.erase(stateVector.begin()); // 删除第一个元素
    //                 }
    //             }
    //         }
    //     }

    //     last_rpm = rpm > 0.001f ? rpm : 0.0f; // 确保小于阈值时为0
    //     // RPM为每分钟圈数，60为一分钟有60秒，相除则为速度，而RPM是每1秒计算一次，所以乘以1
    //     BLE::g_sportCount.store(BLE::g_sportCount.load() + rpm/60.0*1.0);

    //     // for(int i = 0; i < stateVector.size(); i++) {
    //     //     printf("State[%d]: %s\n", i, stateVector[i] ? "True" : "False");
    //     // }
    //     // printf("-----------------------------------------------------------\n");
    // }

    // 间隔有效性验证函数
    bool RPM_Compute::isValidInterval(int interval_ms) {
        // 基础静态阈值检查
        const int ABSOLUTE_MIN = 150;  // 最小物理可能周期
        const int ABSOLUTE_MAX = 3000; // 最大物理可能周期

        // 初步筛选非法值
        if (interval_ms < ABSOLUTE_MIN || interval_ms > ABSOLUTE_MAX) {
            return false;
        }

        // 动态阈值计算（基于历史统计）
        int dynamicMin = ABSOLUTE_MIN;
        int dynamicMax = ABSOLUTE_MAX;

        if (RPM::RPM_Compute::avgCycle > 0) {
            // 使用3σ原则动态调整范围
            dynamicMin = std::max(ABSOLUTE_MIN, (int)(RPM::RPM_Compute::avgCycle - 3 * RPM::RPM_Compute::cycleStdDev));
            dynamicMax = std::min(ABSOLUTE_MAX, (int)(RPM::RPM_Compute::avgCycle + 3 * RPM::RPM_Compute::cycleStdDev));

            // 保证最小波动范围
            dynamicMin = std::max(dynamicMin, (int)(RPM::RPM_Compute::avgCycle * 0.7f));
            dynamicMax = std::min(dynamicMax, (int)(RPM::RPM_Compute::avgCycle * 1.3f));
        }

        // 双重验证
        bool isInStaticRange = (interval_ms >= 250 && interval_ms <= 3000);
        bool isInDynamicRange =
            (interval_ms >= dynamicMin && interval_ms <= dynamicMax);

        if(m_sportType == SportType::BYCLING){
            // 经验值，未来可以补充更多，根据实际的转速范围
            float rpm = 60*1000/interval_ms;
            bool result1 = rpm > 100 && RPM::RPM_Compute::last_rpm < 60;
            bool result2 = rpm > 140 && RPM::RPM_Compute::last_rpm < 100;
            bool result3 = rpm > 170 && RPM::RPM_Compute::last_rpm < 130;

            // 综合判断
            bool result = result1 || result2 || result3;
            if(result)
                return false;
        }

        // 初始阶段优先静态范围
        if (RPM::RPM_Compute::avgCycle < 500){
            return isInStaticRange;
        } else {
            return isInStaticRange && isInDynamicRange;
        }
    }

    // void RPM_Compute::performFFTSmoothingIDF(float *data, int len, float fft_keep_ratio)
    // {
    // // [检查 1] 指针安全
    // if (data == NULL || _fft_work_buf == NULL) return;

    // // [检查 2] 长度越界保护
    //     if (len <= 0 || len > FFT_SIZE) return;

    //     // 直接使用成员变量（已经是绝对对齐的了）
    //     float *y_cf = this->_fft_work_buf; 
        
    //     // --- 步骤 A: 计算平均值 (去直流) ---
    //     float mean = 0.0f;
    //     for (int i = 0; i < len; i++) {
    //         mean += data[i];
    //     }
    //     mean /= (float)len;

    //     // --- 步骤 B: 填充数据 ---
    //     for (int i = 0; i < FFT_SIZE; i++) {
    //         if (i < len) {
    //             // 1. 去直流
    //             float val = data[i] - mean;
                
    //             // 2. 【关键修复】加汉宁窗，消除边缘突变
    //             // 注意：加窗会改变幅度，如果只为了测频（RPM）没问题；
    //             // 如果要精确幅度，后期需要补偿。
    //             float window = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (len - 1)));
                
    //             y_cf[i * 2 + 0] = val * window; 
    //         } else {
    //             y_cf[i * 2 + 0] = 0.0f; // 补零
    //         }
    //         y_cf[i * 2 + 1] = 0.0f;
    //     }

    //     // 3. 执行 FFT 
    //     dsps_fft2r_fc32(y_cf, FFT_SIZE);
    //     dsps_bit_rev_fc32(y_cf, FFT_SIZE);

    //     // 4. 频域滤波
    //     int keep_num = (int)(FFT_SIZE * fft_keep_ratio);
    //     if (keep_num < 2) keep_num = 2;

    //     int start_clear = keep_num;
    //     int end_clear = FFT_SIZE - keep_num;

    //     if (end_clear > start_clear) {
    //         // 使用 memset 可能会更快，但循环更直观
    //         for (int i = start_clear; i < end_clear; i++) {
    //             y_cf[i * 2 + 0] = 0.0f;
    //             y_cf[i * 2 + 1] = 0.0f;
    //         }
    //     }

    //     // 5. IFFT 
    //     // 共轭
    //     for (int i = 0; i < FFT_SIZE; i++) {
    //         y_cf[i * 2 + 1] = -y_cf[i * 2 + 1];
    //     }
    //     // FFT
    //     dsps_fft2r_fc32(y_cf, FFT_SIZE);
    //     dsps_bit_rev_fc32(y_cf, FFT_SIZE);

    //     // 6. 归一化写回
    //     float scale = 1.0f / FFT_SIZE;
    //     for (int i = 0; i < len; i++) {
    //         data[i] = (y_cf[i * 2 + 0] * scale) + mean; 
    //     }

    //     printf("FFT Smoothing Done\n");
    // }

    void RPM_Compute::performFFTSmoothingIDF(float *data, int len, float fft_keep_ratio)
    {
        // [检查] 安全性检查
        if (data == NULL || _fft_work_buf == NULL) return;
        if (len <= 0 || len > FFT_SIZE) return;

        float *y_cf = this->_fft_work_buf; 
        
        // 计算需要补充多少数据 (padding length)
        // 例如：FFT_SIZE(256) - len(200) = 56
        int pad_len = FFT_SIZE - len;
        
        // 防止 len > FFT_SIZE 的异常情况
        if (pad_len < 0) pad_len = 0;

        // ============================================================
        // 步骤 A: 拼接数据 (Stitching / Overlap)
        // 目标：构造一个 256 长度的连续波形
        // 结构：[ 历史数据的末尾(pad_len) ] + [ 当前数据(len) ]
        // ============================================================
        
        // 1. 填充前段 (Padding): 来自历史数据
        if (_history_len > 0 && pad_len > 0) {
            // 如果有历史数据，从历史数据的【末尾】倒着取 pad_len 个
            // 确保取的数据量不超过历史总长
            int copy_amount = (pad_len > _history_len) ? _history_len : pad_len;
            int start_idx = _history_len - copy_amount;
            
            // 复制历史尾部到 FFT 缓冲区的头部
            for (int i = 0; i < copy_amount; i++) {
                y_cf[i * 2 + 0] = _history_buffer[start_idx + i];
                y_cf[i * 2 + 1] = 0.0f;
            }
            
            // 如果历史数据不够填满 pad_len (比如第一帧)，剩下的用历史最后一个值填满(Clamp)
            for (int i = copy_amount; i < pad_len; i++) {
                y_cf[i * 2 + 0] = _history_buffer[_history_len - 1];
                y_cf[i * 2 + 1] = 0.0f;
            }
        } else {
            // 第一帧：没有历史，用当前数据的第一个值填充头部
            // 避免补 0 造成的跳变
            for (int i = 0; i < pad_len; i++) {
                y_cf[i * 2 + 0] = data[0]; 
                y_cf[i * 2 + 1] = 0.0f;
            }
        }

        // 2. 填充后段: 放入当前的实时数据
        // 放在 pad_len 之后的位置
        for (int i = 0; i < len; i++) {
            y_cf[(pad_len + i) * 2 + 0] = data[i];
            y_cf[(pad_len + i) * 2 + 1] = 0.0f;
        }
        
        // ============================================================
        // 步骤 B: 备份数据 (为下一帧做准备)
        // ============================================================
        // 将当前原始数据保存到 _history_buffer
        for (int i = 0; i < len; i++) {
            _history_buffer[i] = data[i];
        }
        _history_len = len;

        // ============================================================
        // 步骤 C: 标准 FFT 流程 (无窗函数)
        // ============================================================
        
        // 1. FFT
        dsps_fft2r_fc32(y_cf, FFT_SIZE);
        dsps_bit_rev_fc32(y_cf, FFT_SIZE);

        // 2. 频域滤波 (Low Pass)
        int keep_num = (int)(FFT_SIZE * fft_keep_ratio);
        if (keep_num < 2) keep_num = 2; // 至少保留直流和基波

        int start_clear = keep_num;
        int end_clear = FFT_SIZE - keep_num;

        // 清零高频区
        if (end_clear > start_clear) {
            // 使用指针或者循环清零都可以
            for (int i = start_clear; i < end_clear; i++) {
                y_cf[i * 2 + 0] = 0.0f;
                y_cf[i * 2 + 1] = 0.0f;
            }
        }

        // 3. IFFT (使用共轭技巧: IFFT(x) = conj(FFT(conj(x)))/N )
        
        // 取共轭
        for (int i = 0; i < FFT_SIZE; i++) {
            y_cf[i * 2 + 1] = -y_cf[i * 2 + 1];
        }
        
        // 再次 FFT
        dsps_fft2r_fc32(y_cf, FFT_SIZE);
        dsps_bit_rev_fc32(y_cf, FFT_SIZE);

        // ============================================================
        // 步骤 D: 提取并写回数据
        // 注意：我们只关心后半段 (对应当前帧 data 的那部分)
        // ============================================================
        
        float scale = 1.0f / FFT_SIZE;
        
        for (int i = 0; i < len; i++) {
            // 输出索引要偏移 pad_len
            int fft_idx = pad_len + i;
            
            // 写回 data
            data[i] = y_cf[fft_idx * 2 + 0] * scale;
        }
    }

    std::vector<float> RPM_Compute::removeTrendMovingAverage(const float* data, int length, int window_size) {
        std::vector<float> detrended_data;
        detrended_data.reserve(length); // 预分配内存，提升效率

        // 如果数据长度不够，直接返回原始数据的拷贝
        if (length < window_size) {
            for (int i = 0; i < length; i++) {
                detrended_data.push_back(data[i]);
            }
            return detrended_data;
        }

        int half_win = window_size / 2;

        for (int i = 0; i < length; ++i) {
            // 1. 计算窗口范围，对应 Python 的 start = max(0, ...), end = min(n, ...)
            int start_idx = std::max(0, i - half_win);
            // 注意：Python 的切片是左闭右开 [start:end)，所以这里 end_idx 作为循环的上限
            int end_idx = std::min(length, i + half_win);

            // 2. 计算窗口内的平均值
            float sum = 0.0f;
            int count = 0;
            
            // 简单的遍历求和 (ESP32S3 算力足够，对于几百个点的窗口通常不需要滑动窗口优化)
            for (int k = start_idx; k < end_idx; ++k) {
                sum += data[k];
                count++;
            }

            float avg = 0.0f;
            if (count > 0) {
                avg = sum / (float)count;
            }

            // 3. 去趋势：原始值 - 平均值
            detrended_data.push_back(data[i] - avg);
        }

        return detrended_data;
    }

    int RPM_Compute::findSignificantPeaks(const float *data, int len, PeakInfo *peaks) {
        if (len < 3 || !data || !peaks) return 0;

        // 统计计算
        float sum = 0.0f, max_val = data[0], min_val = data[0];
        for (int i = 0; i < len; i++) {
            sum += data[i];
            if (data[i] > max_val) max_val = data[i];
            if (data[i] < min_val) min_val = data[i];
        }
        float avg = sum / len;

        // 动态阈值：基于数据离散程度
        float std_dev = 0.0f;
        for (int i = 0; i < len; i++) {
            std_dev += powf(data[i] - avg, 2);
        }
        // 计算标准差
        std_dev = sqrtf(std_dev / len);

        // 信号动态范围检查
        if (max_val - min_val < min_signal_range) return 0;

        int max_count = 0,min_count = 0;
        PeakInfo max_peaks[MAX_PEAKS],min_peaks[MAX_PEAKS];
        float max_threshold = avg + PEAK_COEFFICIENT * std_dev;  // 系数可调
        float min_threshold = avg - PEAK_COEFFICIENT * std_dev;  // 系数可调
        for (int i = 1; i < len - 1; i++) {
            // 平顶峰检测
            bool isPeakStart = (data[i] >= data[i-1] && data[i] > data[i+1]);
            bool isPeakEnd = (data[i] > data[i-1] && data[i] >= data[i+1]);            
            // 严格尖峰
            bool isSharpPeak = (data[i] > data[i-1] && data[i] > data[i+1]);

            // 满足条件的峰值
            if ((isPeakStart || isPeakEnd || isSharpPeak) && 
                data[i] > max_threshold) { 
                // 记录峰
                max_peaks[max_count].index = i;
                max_peaks[max_count].value = data[i];
                max_count++;

                // 跳过平顶区
                if (isPeakStart || isPeakEnd) {
                    while (i+1 < len && data[i+1] == data[i]) i++;
                }
            }

            // 平波谷检测
            bool isValleyStart = (data[i] <= data[i-1] && data[i] < data[i+1]);
            bool isValleyEnd = (data[i] < data[i-1] && data[i] <= data[i+1]);
            // 严格波谷
            bool isSharpValley = (data[i] < data[i-1] && data[i] < data[i+1]);

            // 满足条件的波谷
            if ((isValleyStart || isValleyEnd || isSharpValley) && 
                data[i] < min_threshold) {
                // 记录波谷
                min_peaks[min_count].index = i;
                min_peaks[min_count].value = data[i];

                min_count++;

                // 跳过平顶区
                if (isValleyStart || isValleyEnd) {
                    while (i+1 < len && data[i+1] == data[i]) i++;
                }
            }
        }

        int count = 0;
        std::vector<int> peaksIndex;
        for(int max_index = 0;max_index < max_count;max_index++){
            for(int min_index = 0;min_index < min_count - 1;min_index++){
                if( min_index + 1 <= min_count 
                    && min_peaks[min_index].index < max_peaks[max_index].index 
                    && min_peaks[min_index + 1].index > max_peaks[max_index].index){
                    peaks[count].index = max_peaks[max_index].index;
                    peaks[count].value = max_peaks[max_index].value;
                    peaksIndex.push_back(min_index);
                    count++;
                    // ESP_LOGI("TAG", "---findSignificantPeaks: %d", peaks[count].index);
                    break;
                }
            }
        }

        std::vector<int> smallIndexs;
        int peakIndex = 0;
        const int size = peaksIndex.size();
        // //  如果波峰只有1个，使用此代码会造成RPM不稳定
        // if(size == 1) {
        //     return 1; // 直接返回1个波峰
        // }

        while (peakIndex < size - 1) {
            int start = peakIndex;
            // 找到当前组的结束位置
            int end = start;
            while (end < size - 1 && peaksIndex[end] == peaksIndex[end + 1]) {
                end++;
            }
            
            // 如果组内有多个波峰
            if (end > start) {
                int maxIndex = start;
                // 遍历组内所有波峰，找到最大值的索引
                for (int i = start + 1; i <= end; i++) {
                    if (peaks[i].value > peaks[maxIndex].value) {
                        smallIndexs.push_back(maxIndex); // 标记前一个最大值为删除
                        maxIndex = i;
                    } else {
                        smallIndexs.push_back(i); // 标记当前为删除
                    }
                }
            }
            
            peakIndex = end + 1; // 移动到下一组的开始
        }

        if(!smallIndexs.empty()) {
            std::sort(smallIndexs.begin(), smallIndexs.end(), 
                    [](int a, int b) { return a > b; });

            // 删除重复索引（可选）
            auto last = std::unique(smallIndexs.begin(), smallIndexs.end());
            smallIndexs.erase(last, smallIndexs.end());

            // 按从大到小的顺序删除
            for (int ind : smallIndexs) {
                if (ind < count && ind >= 0) {
                    // 移动后续元素覆盖要删除的元素
                    for (int j = ind; j < count - 1; j++) {
                        peaks[j] = peaks[j + 1];
                    }
                    count--;
                }
            }
        }

        return count;
    }


    // 选择最稳定的周期（基于中位数滤波）
    int RPM_Compute::selectBestCycle(const int *candidates, int count) {
        if (count <= 0)
            return -1;

        // 创建副本避免修改原数组
        int sorted[count];
        memcpy(sorted, candidates, count * sizeof(int));

        // 简单冒泡排序
        for (int i = 0; i < count - 1; i++) {
            for (int j = 0; j < count - i - 1; j++) {
                if (sorted[j] > sorted[j + 1]) {
                    int temp = sorted[j];
                    sorted[j] = sorted[j + 1];
                    sorted[j + 1] = temp;
                }
            }
        }

        if (count % 2 == 0) {
            return (sorted[count / 2 - 1] + sorted[count / 2]) / 2;
        } else {
            return sorted[count / 2];
        }
    }

    float RPM_Compute::calculateRPM(bool isFull,int len,SportType sportType)
    {
        m_sportType = sportType;

        // 1. 检测周期
        int period_ms = detectCyclePeriod(isFull);
        if (period_ms <= 0)
        {
            printf("RPM_Compute::calculateRPM: period_ms = %d\n", period_ms);
            return -1.0f; // 周期无效
        }

        // 2. 计算 RPM
        float rpm = 60000.0f / period_ms;
        if(sportType == SportType::BYCLING){
            if(rpm< 25 || rpm >180){
                printf("RPM_Compute::calculateRPM: rpm = %f\n", rpm);
                rpm = -1;
            }
        }else if(sportType == SportType::WALKING){
            // =====================================================
            // 针对 "M型" 波峰的硬编码修正（带迟滞策略）
            // =====================================================
            
            // 青年人步行很难超过 70-80 RPM (即140-160步频)
            // 一旦超过，大概率是双倍计数误判
            static int upper_threshold = 90;
            static int lower_threshold = 80;

            if (rpm > upper_threshold) 
                is_halving_mode = true;
            else if(rpm < lower_threshold) 
                is_halving_mode = false;

            // 如果处于减半模式，执行修正
            if (is_halving_mode)
                rpm = rpm / 2.0f;
        }

        return rpm;
    }

    int RPM_Compute::detectCyclePeriod(bool isFull,int len) {
        int bufferLen = WINDOW_BUFFER_SIZE;
        if(!isFull)
            bufferLen = len;

        memcpy(_analysisBuffer, angle_buffer, rpm_buffer_block);
        // 1. 数据预处理：FFT平滑 + 去趋势
        performFFTSmoothingIDF(_analysisBuffer, bufferLen, 0.15f);
        removeTrendMovingAverage(_analysisBuffer, bufferLen, (int)(bufferLen / 3));

        // 2. 改进的峰值检测（动态阈值）
        PeakInfo peaks[MAX_PEAKS] = {0};
        int peakCount = findSignificantPeaks(_analysisBuffer, bufferLen, peaks);

        // 3. 周期计算（考虑多候选周期）
        int candidates[MAX_CYCLES] = {0};
        int validCount = 0;

        for (int i = 0; i < peakCount - 1; i++) {
            int interval = (peaks[i + 1].index - peaks[i].index) * 20;
            if (isValidInterval(interval)) {
                candidates[validCount++] = interval;
                if (validCount >= MAX_CYCLES)
                    break;
            }
        }
        return selectBestCycle(candidates, validCount);
    }
}