#ifndef EEPROM_UTILS_H
#define EEPROM_UTILS_H

#if ENABLE_IMU_TYPE_MPU9250
#include <MPU9250.h>
#else
#include <ICM_20948.h>
#endif
#include <EEPROM.h>

// 定义RTC内存中的零偏变量
extern float accel_bias[3];

namespace EEPROM_Utils {
    void writeByte(int address, byte value);

    void writeFloat(int address, float value);

    byte readByte(int address);

    float readFloat(int address);

    void printBytes();

    void setupEEPROM();

#if ENABLE_IMU_TYPE_MPU9250
    void clearCalibration();

    bool isCalibrated();

    void saveCalibration();

    void loadCalibration();

    void printCalibration();
#endif
}

#endif