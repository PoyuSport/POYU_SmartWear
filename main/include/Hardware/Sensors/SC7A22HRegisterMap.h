
// SC7A22H 寄存器定义
#define SC7A22H_I2C_ADDRESS          0x19  // I2C地址

// 通用寄存器地址
#define SC7A22H_REG_WHO_AM_I         0x01
#define SC7A22H_REG_COM_CONF         0x04
#define SC7A22H_REG_PWR_CTRL         0x7D
#define SC7A22H_REG_ACC_CONF         0x40
#define SC7A22H_REG_ACC_RANGE        0x41
#define SC7A22H_REG_DATA_STATUS      0x0B

// 加速度计数据寄存器
#define SC7A22H_REG_ACC_XH           0x0C
#define SC7A22H_REG_ACC_XL           0x0D
#define SC7A22H_REG_ACC_YH           0x0E
#define SC7A22H_REG_ACC_YL           0x0F
#define SC7A22H_REG_ACC_ZH           0x10
#define SC7A22H_REG_ACC_ZL           0x11


#define SC7A22H_REG_ACC_CONF         0x40
#define SC7A22H_REG_ACC_RANGE        0x41
#define SC7A22H_AOI1_AOI2_CFG        0x3F
#define SC7A22H_INT_CFG1             0x06
#define SC7A22H_HPF_LPF_DFG          0x08
#define SC7A22H_AOI1_CFG             0x30
#define SC7A22H_AOI1_THS             0x32
#define SC7A22H_AOI1_DUR             0x33




// 工作模式定义
#define ACC_EN                      0x04  // 加速度计使能

// 加速度计量程定义
#define ACC_RANGE_2G                0x00
#define ACC_RANGE_4G                0x01
#define ACC_RANGE_8G                0x02
#define ACC_RANGE_16G               0x03

