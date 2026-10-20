#pragma once

#include <cmath>
#include <vector>

#define FILTER_SAMPLE_NUM 20

class ButterworthPointFilter {
public:
    // 构造函数：设置采样频率(Hz)和截止频率(Hz)
    ButterworthPointFilter(float sampleFreq = 10.0f, float cutoffFreq = 2.0f) {
        setCoefficients(sampleFreq, cutoffFreq);
        reset();
    }

    // 设置滤波器系数
    void setCoefficients(float sampleFreq, float cutoffFreq) {
        // 计算归一化角频率
        const float omega = 2.0f * M_PI * cutoffFreq / sampleFreq;
        const float tan_half_omega = tanf(omega / 2.0f);
        
        // 计算Butterworth系数 (双线性变换)
        const float sqrt2 = sqrtf(2.0f);
        const float a0 = 1.0f + sqrt2 * tan_half_omega + tan_half_omega * tan_half_omega;
        
        // 分子系数 (b)
        b0 = tan_half_omega * tan_half_omega / a0;
        b1 = 2.0f * b0;
        b2 = b0;
        
        // 分母系数 (a)
        a1 = 2.0f * (tan_half_omega * tan_half_omega - 1.0f) / a0;
        a2 = (1.0f - sqrt2 * tan_half_omega + tan_half_omega * tan_half_omega) / a0;
    }

    // 重置滤波器状态
    void reset() {
        x1 = x2 = 0.0f;
        y1 = y2 = 0.0f;
        buffer.clear();
    }

    // 添加新数据点
    void addData(float value) {
        buffer.push_back(value);
    }

    // 处理数据块并返回最优单点结果
    float processAndGetBestPoint() {
        if (buffer.empty()) return 0.0f;
        
        // 1. 保存当前状态（用于恢复）
        float orig_x1 = x1, orig_x2 = x2;
        float orig_y1 = y1, orig_y2 = y2;
        
        // 2. 处理整个数据块
        std::vector<float> filtered;
        for (float val : buffer) {
            filtered.push_back(process(val));
        }
        
        // 3. 恢复原始状态（不改变滤波器长期状态）
        x1 = orig_x1; x2 = orig_x2;
        y1 = orig_y1; y2 = orig_y2;
        
        // 4. 返回最稳定点（最后1/3部分的平均值）
        return getStableValue(filtered);
    }
    
    // 获取数据块中最稳定的值（最后1/3部分）
    float getStableValue(const std::vector<float>& data) {
        if (data.empty()) return 0.0f;
        
        // 取最后1/3的数据点
        int startIdx = data.size() * 2 / 3;
        int count = data.size() - startIdx;
        
        // 计算平均值
        float sum = 0.0f;
        for (int i = startIdx; i < data.size(); i++) {
            sum += data[i];
        }
        
        return sum / count;
    }

private:
    // 处理单个数据点（内部使用）
    float process(float input) {
        // 计算当前输出
        float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        
        // 更新状态变量
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;
        
        return output;
    }

private:
    // 滤波器系数
    float b0, b1, b2; // 分子系数
    float a1, a2;     // 分母系数
    
    // 状态变量
    float x1, x2; // 输入延迟线
    float y1, y2; // 输出延迟线
    
    // 数据缓冲区
    std::vector<float> buffer;
};


/*
// ==================== 使用示例 ====================
int main() {
    // 1. 创建滤波器 (10Hz采样，2Hz截止)
    ButterworthPointFilter filter(10.0f, 2.0f);
    
    // 2. 添加气压计数据 (8-10个点)
    filter.addData(1013.25f);
    filter.addData(1013.20f);
    filter.addData(1013.15f);
    filter.addData(1013.10f);
    filter.addData(1013.05f);
    filter.addData(1013.00f);
    filter.addData(1012.95f);
    filter.addData(1012.90f);
    // 可选添加更多点
    filter.addData(1012.85f);
    filter.addData(1012.80f);
    
    // 3. 处理并获取最优单点结果
    float bestValue = filter.processAndGetBestPoint();
    printf("Optimal filtered value = %.3f hPa\n", bestValue);
    
    // 4. 重置滤波器准备下一组数据
    filter.reset();
    
    return 0;
}
*/