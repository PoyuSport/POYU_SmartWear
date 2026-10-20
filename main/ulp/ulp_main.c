// #include <stdio.h>
// #include <stdint.h>
// #include <ulp_riscv.h>
// #include <ulp_riscv_utils.h>
// #include <ulp_riscv_i2c.h>
// #include "../include/Hardware/Sensors/SC7A22HRegisterMap.h"  

// // 唤醒阈值设置
// // 假设量程配置为 ±2G 或 ±4G，需根据实际灵敏度调整
// // 若灵敏度未知，建议通过打印 raw_val 调试。这里假设 1g ≈ 4096 (常见值)
// #define SENSITIVITY           16384.0f  
// #define G_THRESHOLD           1.5f     // 唤醒阈值 (1.5g)
// #define DEADZONE_RATIO        0.05f    // 死区

// // 计算判定值的平方 (避免开根号，节省资源)
// #define THRESHOLD_SQ          ((int32_t)(G_THRESHOLD * SENSITIVITY) * (int32_t)(G_THRESHOLD * SENSITIVITY))

// // 采样周期 (RTC时钟周期)
// #define SAMPLE_PERIOD         7812       // 1秒

// // ---------------- 全局变量 (主核可读) ----------------
// int32_t g_acc_x, g_acc_y, g_acc_z;
// uint8_t g_status_reg;

// int main(void) {
//     while(1) {
//         uint8_t status = 0;
        
//         ulp_riscv_i2c_master_set_slave_addr(SC7A22H_I2C_ADDRESS);
//         // 设置读取地址
//         ulp_riscv_i2c_master_set_slave_reg_addr(SC7A22H_REG_DATA_STATUS);
//         ulp_riscv_i2c_master_read_from_device(&status, 1);
        
//         g_status_reg = status; // 供调试查看

//         // 解析状态: 必须满足位掩码(即 bit0为1)
//         if ((status & 0x01) == 0x01){
//             // 读取 6字节 加速度数据
//             // 你的代码逻辑: 0x0C 开始, 顺序为 X_H, X_L, Y_H, Y_L, Z_H, Z_L
//             uint8_t raw[6];
//             ulp_riscv_i2c_master_set_slave_reg_addr(SC7A22H_REG_ACC_XH);
//             ulp_riscv_i2c_master_read_from_device(raw, 6);

//             // X轴
//             int16_t x_val = (int16_t)((raw[0] << 8) | raw[1]);
//             // Y轴
//             int16_t y_val = (int16_t)((raw[2] << 8) | raw[3]);
//             // Z轴
//             int16_t z_val = (int16_t)((raw[4] << 8) | raw[5]);

//             // 更新全局变量供主核查看
//             g_acc_x = x_val;
//             g_acc_y = y_val;
//             g_acc_z = z_val;

//             // 运动检测算法 (平方和比较法)
//             // 将中间计算结果转为 uint32_t，防止变为负数
//             uint32_t sum_sq = (uint32_t)((int32_t)x_val * x_val) + 
//                             (uint32_t)((int32_t)y_val * y_val) + 
//                             (uint32_t)((int32_t)z_val * z_val);

//             // 阈值判断
//             if (sum_sq > (uint32_t)THRESHOLD_SQ) {
//                 // 唤醒主处理器
//                 ulp_riscv_wakeup_main_processor();
//             }
//         }

//         // 进入低功耗等待，直到下一次采样
//         ulp_riscv_delay_cycles(ULP_RISCV_CYCLES_PER_MS*100);
//     }

//     return 0;
// }