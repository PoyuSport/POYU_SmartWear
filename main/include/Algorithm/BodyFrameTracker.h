#pragma once

#include <Arduino.h>
#include <ArduinoEigen.h>  // 安装ArduinoEigen库
#include <ArduinoEigenDense.h>
#include <ArduinoEigenSparse.h>
#include <SW_define.h>  
#include <SW_struct.h>

 // 用于矩阵求逆
 // 用于四元数和旋转矩阵



// 欧拉角结构体
struct EulerAngles {
    float roll, pitch, yaw;
    EulerAngles() : roll(0), pitch(0), yaw(0) {}
    EulerAngles(float r, float p, float y) : roll(r), pitch(p), yaw(y) {}
};

// 带通滤波器实现
class BandpassFilter {
private:
    float fs; // 采样频率
    float f1, f2; // 通带频率范围
    float a0, a1, a2, b1, b2; // 滤波器系数
    
    // 滤波器状态
    float x1 = 0, x2 = 0;
    float y1 = 0, y2 = 0;
    
public:
  BandpassFilter(float sampleRate, float lowFreq, float highFreq);
  void calculateCoefficients();
  float filter(float input);

};

// 姿态转换类
class BodyFrameTracker {
private:
    Matrix3f R_sensor_to_body;
    bool calibrated;
    std::vector<Vector3f> accel_buffer;
    size_t buffer_size;
    
public:
  BodyFrameTracker(size_t buffer_size = WINDOW_BUFFER_SIZE);
  void addAccelData(const Vector3f &accel);
  bool calibrate();
  EulerAngles convertToBodyFrame(const Quaternionf &sensor_quat);
  bool isCalibrated() const;
  void resetCalibration();
};