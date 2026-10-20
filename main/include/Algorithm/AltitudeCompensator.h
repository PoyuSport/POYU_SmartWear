#pragma once
#include <BasicLinearAlgebra.h>
#include <array>

class AltitudeCompensator {
  public:
    struct Output {
        float filtered_altitude; // 补偿后高度 (米)
        float vertical_speed;    // 垂直速度 (米/秒)
        float dynamic_pressure;  // 动态压力补偿值 (Pa)
    };

    /**
     * @brief 构造函数
     * @param sea_level_pressure 海平面气压 (hPa)
     * @param Q_mult 过程噪声系数 (默认1.0)
     */
    AltitudeCompensator(float sea_level_pressure = 1013.25f,
                        float Q_mult = 1.0f)
        : sea_level_hpa(sea_level_pressure), Q_multiplier(Q_mult) {}

    /**
     * @brief 更新传感器数据
     * @param pressure 原始气压 (hPa)
     * @param accel_z 垂直加速度 (m/s²)
     * @param temperature 温度 (°C)
     * @param dt 时间间隔 (秒)
     */
    void update(float pressure, float accel_z, float temperature, float dt) {
        // 温度补偿
        float compensated_pressure =
            pressureCompensation(pressure, temperature);

        // 动态压力补偿
        float dynamic_comp = calculateDynamicPressureComp(accel_z);
        float static_pressure = compensated_pressure - dynamic_comp;

        // 卡尔曼滤波
        kalmanUpdate(static_pressure, accel_z, dt);

        // 保存动态压力值
        output.dynamic_pressure = dynamic_comp * 100; // 转换为Pa
    }

    /**
     * @brief 获取补偿结果
     */
    Output getResult() const { return output; }

    /**
     * @brief 设置海平面基准气压
     */
    void setSeaLevel(float pressure_hpa) {
        sea_level_hpa = pressure_hpa;
        resetKalman();
    }

    /**
     * @brief 重置滤波器状态
     */
    void resetKalman() { kalman_initialized = false; }

  private:
    BLA::Matrix<3> x_est; // 状态向量 [高度, 速度, 加速度偏差]
    BLA::Matrix<3, 3> P_est;
    float sea_level_hpa;
    Output output;
    bool kalman_initialized = false;
    float Q_multiplier;

    // 温度补偿参数
    const float PRESSURE_TEMP_COEFF = 0.034; // hPa/°C

    // 动态压力模型参数
    const float ROTOR_RADIUS = 0.1f;  // 螺旋桨半径 (米)
    const float AIR_DENSITY = 1.225f; // 空气密度 (kg/m³)

    void kalmanUpdate(float pressure, float accel_z, float dt) {
        // 气压高度转换
        float altitude =
            44330.0f * (1.0f - powf(pressure / sea_level_hpa, 0.1903f));

        if (!kalman_initialized) {
            initializeKalman(altitude);
            return;
        }

        // 状态转移矩阵
        BLA::Matrix<3, 3> F = {1, dt, 0.5 * dt * dt, 0, 1, dt, 0, 0, 1};

        // 控制矩阵 (考虑重力)
        BLA::Matrix<3> B = {0.5 * dt * dt, dt, 0};
        float u = accel_z - 9.80665f; // 去除重力加速度

        // 预测步骤
        x_est = F * x_est + B * u;
        P_est = F * P_est * ~F + Q(dt);

        // 更新步骤
        BLA::Matrix<1, 3> H = {1, 0, 0}; // 仅观测高度
        float y = altitude - (H * x_est)(0);
        float S = (H * P_est * ~H)(0, 0) + R();
        BLA::Matrix<3, 1> K = P_est * ~H * (1.0f / S);

        x_est += K * y;
        P_est = (BLA::Identity<3, 3>() - K * H) * P_est;

        // 更新输出
        output.filtered_altitude = x_est(0);
        output.vertical_speed = x_est(1);
    }

    float pressureCompensation(float pressure, float temp) {
        // 温度漂移补偿
        return pressure - (temp * PRESSURE_TEMP_COEFF);
    }

    float calculateDynamicPressureComp(float accel_z) {
        // 简化旋翼下洗流模型: ΔP = 0.5 * ρ * (ω × r)^2
        // 假设加速度与旋翼转速平方成正比
        float omega_sq = fabs(accel_z) / ROTOR_RADIUS;
        return 0.5f * AIR_DENSITY * omega_sq / 100.0f; // 转换为hPa
    }

    void initializeKalman(float initial_altitude) {
        x_est = {initial_altitude, 0.0f, 0.0f};
        P_est = {1.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 0.0f, 0.01f};
        kalman_initialized = true;
    }

    BLA::Matrix<3, 3> Q(float dt) {
        float q = Q_multiplier * 0.1f;
        return {q * powf(dt, 5) / 20, q * powf(dt, 4) / 8, q * powf(dt, 3) / 6,
                q * powf(dt, 4) / 8,  q * powf(dt, 3) / 3, q * powf(dt, 2) / 2,
                q * powf(dt, 3) / 6,  q * powf(dt, 2) / 2, q * dt};
    }

    float R() {
        return 0.5f; // 测量噪声协方差
    }
};