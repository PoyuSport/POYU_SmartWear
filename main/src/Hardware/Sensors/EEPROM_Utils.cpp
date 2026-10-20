#include <EEPROM.h>
#include <EEPROM_Utils.h>
#include <Initialize.h>
#include <driver/rtc_io.h>
#include <ICM_20948.h>

const uint8_t EEPROM_SIZE = 1 + sizeof(float) * 3 * 4;

const char *TAG = "ARDUINO_LOG";

#if ENABLE_IMU_TYPE_MPU9250
extern MPU9250 imu;
#else
extern ICM_20948_I2C imu;
#endif

float accel_bias[3] = {0.0f, 0.0f, 0.0f};

enum EEP_ADDR {
    EEP_CALIB_FLAG = 0x00,
    EEP_ACC_BIAS = 0x01,
    EEP_GYRO_BIAS = 0x0D,
    EEP_MAG_BIAS = 0x19,
    EEP_MAG_SCALE = 0x25
};

namespace EEPROM_Utils {
    void writeByte(int address, byte value) { EEPROM.put(address, value); }

    void writeFloat(int address, float value) { EEPROM.put(address, value); }

    byte readByte(int address) {
        byte valueIn = 0;
        EEPROM.get(address, valueIn);
        return valueIn;
    }

    float readFloat(int address) {
        float valueIn = 0;
        EEPROM.get(address, valueIn);
        return valueIn;
    }

#if ENABLE_IMU_TYPE_MPU9250
    void clearCalibration() { writeByte(EEP_CALIB_FLAG, 0); }

    bool isCalibrated() { return (readByte(EEP_CALIB_FLAG) == 0x01); }

    void saveCalibration() {
        Serial.println("Write calibrated parameters to EEPROM");
        writeByte(EEP_CALIB_FLAG, 1);
        writeFloat(EEP_ACC_BIAS + 0, imu.getAccBias(0));
        writeFloat(EEP_ACC_BIAS + 4, imu.getAccBias(1));
        writeFloat(EEP_ACC_BIAS + 8, imu.getAccBias(2));
        writeFloat(EEP_GYRO_BIAS + 0, imu.getGyroBias(0));
        writeFloat(EEP_GYRO_BIAS + 4, imu.getGyroBias(1));
        writeFloat(EEP_GYRO_BIAS + 8, imu.getGyroBias(2));
        writeFloat(EEP_MAG_BIAS + 0, imu.getMagBias(0));
        writeFloat(EEP_MAG_BIAS + 4, imu.getMagBias(1));
        writeFloat(EEP_MAG_BIAS + 8, imu.getMagBias(2));
        writeFloat(EEP_MAG_SCALE + 0, imu.getMagScale(0));
        writeFloat(EEP_MAG_SCALE + 4, imu.getMagScale(1));
        writeFloat(EEP_MAG_SCALE + 8, imu.getMagScale(2));
    #if defined(ESP_PLATFORM) || defined(ESP8266)
        EEPROM.commit();
    #endif
    }

    void loadCalibration() {
        Serial.println("Load calibrated parameters from EEPROM");
        if (isCalibrated()) {
            Serial.println("calibrated? : YES");
            Serial.println("load calibrated values");
            imu.setAccBias(readFloat(EEP_ACC_BIAS + 0), readFloat(EEP_ACC_BIAS + 4),
                        readFloat(EEP_ACC_BIAS + 8));
            imu.setGyroBias(readFloat(EEP_GYRO_BIAS + 0),
                            readFloat(EEP_GYRO_BIAS + 4),
                            readFloat(EEP_GYRO_BIAS + 8));
            imu.setMagBias(readFloat(EEP_MAG_BIAS + 0), readFloat(EEP_MAG_BIAS + 4),
                        readFloat(EEP_MAG_BIAS + 8));
            imu.setMagScale(readFloat(EEP_MAG_SCALE + 0),
                            readFloat(EEP_MAG_SCALE + 4),
                            readFloat(EEP_MAG_SCALE + 8));
        } else {
            Serial.println("calibrated? : NO");
            Serial.println("load default values");
            imu.setAccBias(0., 0., 0.);
            imu.setGyroBias(0., 0., 0.);
            imu.setMagBias(0., 0., 0.);
            imu.setMagScale(1., 1., 1.);
        }
        accel_bias[0] = readFloat(EEP_ACC_BIAS + 0)/ MPU9250::CALIB_ACCEL_SENSITIVITY;
        accel_bias[1] = readFloat(EEP_ACC_BIAS + 4)/ MPU9250::CALIB_ACCEL_SENSITIVITY;
        accel_bias[2] = readFloat(EEP_ACC_BIAS + 8)/ MPU9250::CALIB_ACCEL_SENSITIVITY;
    }

    void printCalibration() {
        // 替换后的日志输出 
        Serial.printf("\n=== Calibration Parameters ===");
        Serial.printf("Calibrated? : %s", readByte(EEP_CALIB_FLAG) ? "YES" : "NO");

        Serial.printf("Acc Bias X  : %.3f mg",
                readFloat(EEP_ACC_BIAS + 0) * 1000.f /
                    MPU9250::CALIB_ACCEL_SENSITIVITY);
        Serial.printf("Acc Bias Y  : %.3f mg",
                readFloat(EEP_ACC_BIAS + 4) * 1000.f /
                    MPU9250::CALIB_ACCEL_SENSITIVITY);
        Serial.printf("Acc Bias Z  : %.3f mg",
                readFloat(EEP_ACC_BIAS + 8) * 1000.f /
                    MPU9250::CALIB_ACCEL_SENSITIVITY);

        Serial.printf("Gyro Bias X : %.3f deg/s",
                readFloat(EEP_GYRO_BIAS + 0) / MPU9250::CALIB_GYRO_SENSITIVITY);
        Serial.printf("Gyro Bias Y : %.3f deg/s",
                readFloat(EEP_GYRO_BIAS + 4) / MPU9250::CALIB_GYRO_SENSITIVITY);
        Serial.printf("Gyro Bias Z : %.3f deg/s",
                readFloat(EEP_GYRO_BIAS + 8) / MPU9250::CALIB_GYRO_SENSITIVITY);

        Serial.printf("Mag Bias X  : %.1f μT", readFloat(EEP_MAG_BIAS + 0));
        Serial.printf("Mag Bias Y  : %.1f μT", readFloat(EEP_MAG_BIAS + 4));
        Serial.printf("Mag Bias Z  : %.1f μT", readFloat(EEP_MAG_BIAS + 8));

        Serial.printf("Mag Scale X : %.3f", readFloat(EEP_MAG_SCALE + 0));
        Serial.printf("Mag Scale Y : %.3f", readFloat(EEP_MAG_SCALE + 4));
        Serial.printf("Mag Scale Z : %.3f", readFloat(EEP_MAG_SCALE + 8));
    }
#endif

    void printBytes() {
        for (size_t i = 0; i < EEPROM_SIZE; ++i)
            Serial.println(readByte(i), HEX);
    }

    void setupEEPROM() {
        Serial.println("EEPROM start");

#if ENABLE_IMU_TYPE_MPU9250
        if (!isCalibrated()) {
            Serial.println("Need Calibration!!");
        }
        Serial.println("EEPROM calibration value is : ");
        printCalibration();
        Serial.println("Loaded calibration value is : ");
        loadCalibration();
#endif
    }
}