
#include "SW_define.h"
#include "SW_struct.h"
#define DL_DATA_SHAPE 12

namespace DLFeatureSet {
  // 深度学习算法输入特征集及特征集变量
  struct FeatureSet {
    float pitch ;             // IMU设备坐标系下的俯仰角
    float roll  ;             // IMU设备坐标系下的横滚角
    float yaw   ;             //  IMU设备坐标系下的偏航角

    float accel_x;            // 加速度 X    加速度为东北天坐标系
    float accel_y;            // 加速度 Y
    float accel_z;            // 加速度 Z

    float gyro_x;             // 角速度 X   角速度为东北天坐标系    
    float gyro_y;             // 角速度 Y
    float gyro_z;             // 角速度 Z

    float vert_line_accel;    // 纵向线性加速度

    float accel_mag;         // 加速度模长
    float hor_accel;          // 水平方向加速度强度
    float vert_percent;       // 垂直占比
  };

  // 深度学习特征
  class Feature{
    public:
      Vector3f accel; // 加速度
      Vector3f gyro;  // 角速度
      Vector3f line_accel; // 线性加速度
      float pitch;    // IMU设备坐标系下的俯仰角
      float roll;     // IMU设备坐标系下的横滚角

    public:

      Feature(){}
      ~Feature(){}

      // 设置加速度和角速度
      void setAccelGyro(const Vector3f &accel, const Vector3f &gyro) {
        this->accel = accel;
        this->gyro = gyro;
      } 

      // 设置俯仰角和横滚角
      void setPitchRoll(float pitch, float roll) {          
        this->pitch = pitch;
        this->roll = roll;
      }

      void setLineAccel(const Vector3f &line_accel) {
        this->line_accel = line_accel;
      }

      // 获取东北天坐标系下的加速度
      Vector3f getENUAccel() const {
        return Vector3f(-accel.x(), accel.y(), accel.z());
      }

      // 获取东北天坐标系下的角速度
      Vector3f getENUGyro() const {
        return Vector3f(gyro.x(), -gyro.y(), -gyro.z()); // 角速度为设备坐标系
      }

      // 总体加速度长度（三轴加速度取模）
      float getAccelMagnitude() const {
        return sqrt(accel.x() * accel.x() + accel.y() * accel.y() + accel.z() * accel.z());
      }

      // 垂直线性加速度
      float getVerticalLineAccel() const {
        return line_accel.z(); 
      }

      // 水平方向加速度
      float getHorizontalAccel() const {
        return sqrt(line_accel.x() * line_accel.x() + line_accel.y() * line_accel.y());
      }

      // 垂直占比
      float getVerticalPercent() const {
        float totalAccel = getAccelMagnitude();
        if (totalAccel == 0) return 0.0f; // 防止除以零
        return fabs(accel.z() / totalAccel);
      }
  };
}

