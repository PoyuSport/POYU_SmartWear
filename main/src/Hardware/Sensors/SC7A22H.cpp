#include "SC7A22H.h"

char i2cTxBuffer[16];
char i2cRxBuffer[16];

SoftWire softWire(1, 2);

// 写入寄存器函数
bool writeRegister(uint8_t reg, uint8_t value) {
    softWire.beginTransmission(SC7A22H_I2C_ADDRESS);
    softWire.write(reg);
    softWire.write(value);
    uint8_t result = softWire.endTransmission();
    
    if (result != 0) {
        return false;
    }
    return true;
}

// 读取寄存器函数
uint8_t readRegister(uint8_t reg) {
    softWire.beginTransmission(SC7A22H_I2C_ADDRESS);
    softWire.write(reg);
    if (softWire.endTransmission(false) != 0) {
        return 0;
    }
    
    uint8_t bytesRead = softWire.requestFrom(SC7A22H_I2C_ADDRESS, (uint8_t)1);
    if (bytesRead == 1) {
        return softWire.read();
    }
    return 0;
}

// 读取多个寄存器
bool readRegisters(uint8_t reg, uint8_t* data, uint8_t length) {
    softWire.beginTransmission(SC7A22H_I2C_ADDRESS);
    softWire.write(reg);
    if (softWire.endTransmission(false) != 0) {
        return false;
    }
    
    uint8_t bytesRead = softWire.requestFrom(SC7A22H_I2C_ADDRESS, length);
    if (bytesRead == length) {
        for (uint8_t i = 0; i < length; i++) {
            data[i] = softWire.read();
        }
        return true;
    }
    
    printf("Read registers failed, expected %d, got %d\n", length, bytesRead);
    return false;
}

// 检查设备连接
bool checkConnection() {
    uint8_t whoami = readRegister(SC7A22H_REG_WHO_AM_I);
    printf("WHO_AM_I: 0x%02X\n", whoami);
    
    if ( whoami == 0x18) {
        printf("Device responded, continuing initialization...");
        return true;
    }
    return false;
}

// 软件复位
void softwareReset() {
    printf("Performing software reset...");
    // 连续写入2次0xA5到SOFT_RST寄存器
    writeRegister(0x4A, 0xA5);
    vTaskDelay(pdMS_TO_TICKS(10));
    writeRegister(0x4A, 0xA5);
    vTaskDelay(pdMS_TO_TICKS(50)); // 等待复位完成
}

// 初始化传感器
bool initializeSensor() {
    printf("Initializing SC7A22H...");
    
    // 1. 检查设备连接
    if (!checkConnection()) {
        printf("SC7A22H not found!");
        return false;
    }
    
    // 2. 软件复位
    softwareReset();
    
    // 3. 配置COM_CONF寄存器
    // BDU=1 (块数据更新), Addr_Auto=1 (地址自增)
    if (!writeRegister(SC7A22H_REG_COM_CONF, 0x50)) {
        printf("Failed to configure COM_CONF");
        return false;
    }
    
    // 4. 配置加速度计
    // ACC_CONF (0x40): 高性能模式, ODR=100Hz, 带宽配置
    // 0x88 = 0b10001000 (高性能模式, ODR=100Hz)
    // 低功耗       00100111              50HZ
    if (!writeRegister(SC7A22H_REG_ACC_CONF, (0 << 7) | 0x26)) {
        printf("Failed to configure ACC_CONF");
        return false;
    }
    
    // 5. 配置加速度计量程 ±2g
    if (!writeRegister(SC7A22H_REG_ACC_RANGE, ACC_RANGE_2G)) {
        printf("Failed to configure ACC_RANGE");
        return false;
    }
    
    // 8. 使能传感器
    // PWR_CTRL (0x7D): 使能加速度计
    if (!writeRegister(SC7A22H_REG_PWR_CTRL, ACC_EN)) {
        printf("Failed to enable sensors");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // 等待传感器稳定

    // 9. 验证配置
    uint8_t acc_conf = readRegister(SC7A22H_REG_ACC_CONF);
    uint8_t pwr_ctrl = readRegister(SC7A22H_REG_PWR_CTRL);
    
     printf("ACC_CONF: 0x%02X\n", acc_conf);
    
    printf("SC7A22H initialization completed");
    return true;
}

// 读取加速度数据
bool readAcceleration(float &x, float &y, float &z) {
    uint8_t data[6];
    
    // 检查数据是否就绪
    uint8_t status = readRegister(SC7A22H_REG_DATA_STATUS);
    printf("Data Status: 0x%02X\n", status);
    if (!(status & 0x01)) { // 检查DRDY_ACC位
        printf("Accelerometer data not ready");
        return false;
    }
    
    // 读取加速度数据 (6字节: XH,XL,YH,YL,ZH,ZL)
    if (readRegisters(SC7A22H_REG_ACC_XH, data, 6)) {
        // 组合16位数据
        int16_t raw_x = (int16_t)((data[0] << 8) | data[1]);
        int16_t raw_y = (int16_t)((data[2] << 8) | data[3]);
        int16_t raw_z = (int16_t)((data[4] << 8) | data[5]);
        
        // 转换为g值 (±2G量程，灵敏度为16384 LSB/g)
        x = (float)raw_x /16384.0f;
        y = (float)raw_y / 16384.0f;
        z = (float)raw_z / 16384.0f;
        
#if ENABLE_DEBUG
        Serial.printf("Raw data - X: %d, Y: %d, Z: %d\n", raw_x, raw_y, raw_z);
        Serial.printf("Acceleration - X: %.3fg, Y: %.3fg, Z: %.3fg\n", x, y, z);
#endif
        return true;
    }
    return false;
}

// 0x40,0x07,
// 0x41,0x00,  //+-2g
// 0x3F,0x10, //HPIS1
// 0x06,0x0B, //AOI中断on int1
// 0x08,0xC0, //高通
// 0x30,0x2A, //x,y,z高事件或检测
// 0x32,0x05, //检测门限: 1-127, 值越小, 灵敏度越高
// 0x33,0x00

void motionSetting(){
    writeRegister(SC7A22H_REG_ACC_CONF,0x07);            // ODR2,ODR1,ODR0  (高性能模式/低功耗模式(50Hz))
    vTaskDelay(pdMS_TO_TICKS(10));
    writeRegister(SC7A22H_REG_ACC_RANGE,0x00);           // +/-2G
    vTaskDelay(pdMS_TO_TICKS(10));
    writeRegister(SC7A22H_AOI1_AOI2_CFG,0x10);           // HPIS1(滤波使能)
    vTaskDelay(pdMS_TO_TICKS(10));
    writeRegister(SC7A22H_INT_CFG1,0x0B);                // INT1_SEL3,INT1_SEL1,INT1_SEL0  (AOI1 中断在 INT1 上)
    vTaskDelay(pdMS_TO_TICKS(10));
    writeRegister(SC7A22H_HPF_LPF_DFG,0xC0);             // HPCF1(高通截止频率选择),FDS(内部高通滤波以后的数据输出到数据寄存器或 FIFO)
    vTaskDelay(pdMS_TO_TICKS(10));  
    writeRegister(SC7A22H_AOI1_CFG,0x15);                // x,y,z低事件或检测
    vTaskDelay(pdMS_TO_TICKS(10));
    writeRegister(SC7A22H_AOI1_THS,0x18);                // 
    vTaskDelay(pdMS_TO_TICKS(10));  
    writeRegister(SC7A22H_AOI1_DUR,0x00);                // 持续时间计数值
    vTaskDelay(pdMS_TO_TICKS(50));
}