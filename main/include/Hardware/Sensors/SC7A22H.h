#include <Arduino.h>
#include <math.h>
#include "SoftWire.h"
#include "SC7A22HRegisterMap.h"

extern SoftWire softWire;
extern char i2cTxBuffer[16];
extern char i2cRxBuffer[16];

// 写入寄存器函数
bool writeRegister(uint8_t reg, uint8_t value);
// 读取寄存器函数
uint8_t readRegister(uint8_t reg);

// 读取多个寄存器
bool readRegisters(uint8_t reg, uint8_t *data, uint8_t length);

// 检查设备连接
bool checkConnection();

// 软件复位
void softwareReset();

// 初始化传感器
bool initializeSensor();

// 读取加速度数据
bool readAcceleration(float &x, float &y, float &z);

// 运动设置/AOI中断X,Y,Z方向低电平中断
void motionSetting();