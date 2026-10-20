#include <BodyFrameTracker.h>
#include <Arduino.h>
#include <ArduinoEigen.h>  // 安装ArduinoEigen库
#include <ArduinoEigenDense.h>
#include <ArduinoEigenSparse.h> // 用于矩阵求逆
#include <SW_define.h>  

// 从加速度数据提取重力方向
Vector3f get_vertical_axis(const std::vector<Vector3f>& accel_data) {
    Vector3f sum = Vector3f::Zero();
    for (const auto& accel : accel_data) {
        sum += accel;
    }
    Vector3f gravity = sum / accel_data.size();
    return gravity.normalized();
}

// 从步态加速度特征检测前进方向
Vector3f detect_forward_direction(const std::vector<Vector3f>& accel_data, 
                                 const Vector3f& gravity_vector) {
    // 1. 去除重力分量
    std::vector<Vector3f> linear_accel;
    linear_accel.reserve(accel_data.size());
    
    for (const auto& accel : accel_data) {
        float dot = accel.dot(gravity_vector);
        Vector3f gravity_component = gravity_vector * dot;
        linear_accel.push_back(accel - gravity_component);
    }
    
    // 2. 带通滤波（聚焦步频0.8-2.5Hz）
    BandpassFilter filter_x(50, 0.8, 2.5);
    BandpassFilter filter_y(50, 0.8, 2.5);
    BandpassFilter filter_z(50, 0.8, 2.5);
    
    std::vector<Vector3f> filtered_accel;
    for (const auto& accel : linear_accel) {
        filtered_accel.push_back(Vector3f(
            filter_x.filter(accel.x()),
            filter_y.filter(accel.y()),
            filter_z.filter(accel.z())
        ));
    }
    
    // 3. 计算水平面加速度能量
    std::vector<float> horizontal_energy;
    for (const auto& accel : filtered_accel) {
        // 只考虑XY平面 (x和y分量)
        float energy = accel.x() * accel.x() + accel.y() * accel.y();
        horizontal_energy.push_back(energy);
    }
    
    // 计算75%分位数
    std::vector<float> sorted_energy = horizontal_energy;
    std::sort(sorted_energy.begin(), sorted_energy.end());
    float threshold = sorted_energy[sorted_energy.size() * 3 / 4];
    
    // 4. 提取高能量点
    std::vector<Vector3f> high_energy_points;
    for (size_t i = 0; i < filtered_accel.size(); ++i) {
        if (horizontal_energy[i] > threshold) {
            // 只保留水平分量 (x, y)
            high_energy_points.push_back(Vector3f(
                filtered_accel[i].x(),
                filtered_accel[i].y(),
                0.0f
            ));
        }
    }
    
    // 5. 主成分分析确定主导方向
    if (high_energy_points.empty()) {
        return Vector3f(1, 0, 0); // 默认前进方向
    }
    
    // 计算均值
    Vector3f mean = Vector3f::Zero();
    for (const auto& p : high_energy_points) {
        mean += p;
    }
    mean /= high_energy_points.size();
    
    // 计算协方差矩阵
    Matrix3f cov = Matrix3f::Zero();
    for (const auto& p : high_energy_points) {
        Vector3f diff = p - mean;
        cov += diff * diff.transpose();
    }
    cov /= high_energy_points.size();
    
    // 特征分解（只取最大特征值对应的特征向量）
    Eigen::SelfAdjointEigenSolver<Matrix3f> eigensolver(cov);
    if (eigensolver.info() != Eigen::Success) {
        return Vector3f(1, 0, 0); // 默认前进方向
    }
    
    // 最大特征值对应的特征向量
    Vector3f X_body = eigensolver.eigenvectors().col(2);
    return X_body.normalized();
}

// 四元数转欧拉角
EulerAngles quaternionToEuler(const Quaternionf& q) {
    EulerAngles angles;
    
    // 四元数到欧拉角转换 (ZYX顺序)
    float sinr_cosp = 2 * (q.w() * q.x() + q.y() * q.z());
    float cosr_cosp = 1 - 2 * (q.x() * q.x() + q.y() * q.y());
    angles.roll = std::atan2(sinr_cosp, cosr_cosp);
    
    float sinp = 2 * (q.w() * q.y() - q.z() * q.x());
    if (std::abs(sinp) >= 1) {
        angles.pitch = std::copysign(M_PI / 2, sinp); // 使用90度
    } else {
        angles.pitch = std::asin(sinp);
    }
    
    float siny_cosp = 2 * (q.w() * q.z() + q.x() * q.y());
    float cosy_cosp = 1 - 2 * (q.y() * q.y() + q.z() * q.z());
    angles.yaw = std::atan2(siny_cosp, cosy_cosp);
    
    return angles;
}

BandpassFilter::BandpassFilter(float sampleRate, float lowFreq, float highFreq)
  : fs(sampleRate), f1(lowFreq), f2(highFreq) {
  calculateCoefficients();
}

void BandpassFilter::calculateCoefficients() {
    float w1 = 2 * M_PI * f1 / fs;
    float w2 = 2 * M_PI * f2 / fs;
    float alpha = sinf(w2 - w1) / (2 * cosf((w2 + w1)/2));
    
    a0 = alpha;
    a1 = 0;
    a2 = -alpha;
    b1 = -2 * cosf((w2 + w1)/2);
    b2 = 1 - alpha;
}

float BandpassFilter::filter(float input) {
    float output = a0 * input + a1 * x1 + a2 * x2 - b1 * y1 - b2 * y2;
    
    // 更新状态
    x2 = x1;
    x1 = input;
    y2 = y1;
    y1 = output;
    
    return output;
}


BodyFrameTracker::BodyFrameTracker(size_t buffer_size)
  : calibrated(false), buffer_size(buffer_size) {
  accel_buffer.reserve(buffer_size);
  R_sensor_to_body = Matrix3f::Identity();
}

// 添加加速度数据
void BodyFrameTracker::addAccelData(const Vector3f& accel) {
    accel_buffer.push_back(accel);
    if (accel_buffer.size() > buffer_size) {
        accel_buffer.erase(accel_buffer.begin());
    }
}

// 执行校准
bool BodyFrameTracker::calibrate() {
    if (accel_buffer.size() < WINDOW_BUFFER_SIZE) { 
        return false;
    }
    
    Serial.println("开始校准人体坐标系...");

    // 1. 获取垂直轴
    Vector3f gravity = get_vertical_axis(accel_buffer);
    Vector3f Z_body = -gravity;
    
    // 2. 检测前进方向
    Vector3f X_body = detect_forward_direction(accel_buffer, gravity);
    
    // 3. 计算左右方向 (Y轴)
    Vector3f Y_body = Z_body.cross(X_body).normalized();
    
    // 4. 重建正交坐标系
    X_body = Y_body.cross(Z_body).normalized();
    
    // 5. 构建人体坐标系基向量矩阵
    Matrix3f body_basis;
    body_basis.col(0) = X_body;
    body_basis.col(1) = Y_body;
    body_basis.col(2) = Z_body;
    
    // 6. 传感器坐标系（假设为I）
    Matrix3f sensor_basis = Matrix3f::Identity();
    
    // 7. 计算旋转矩阵
    R_sensor_to_body = body_basis * sensor_basis.inverse();
    
    calibrated = true;
    return true;
}

// 将传感器姿态转换到人体坐标系
// 函数的作用:将传感器自身的姿态（由四元数表示）转换到人体坐标系，并返回人体坐标系下的欧拉角（roll, pitch, yaw）
// 返回值:人体坐标系下的欧拉角（roll, pitch, yaw）
// 意义:无论传感器如何安装在人体上（口袋、手臂等），最终都能得到以人体为参考系的姿态数据
EulerAngles BodyFrameTracker::convertToBodyFrame(const Quaternionf& sensor_quat) {
    if (!calibrated) {
        return EulerAngles();
    }
    
    // 1. 转换四元数到旋转矩阵
    Matrix3f R_sensor = sensor_quat.toRotationMatrix();
    
    // 2. 转换到人体坐标系
    Matrix3f R_body = R_sensor_to_body * R_sensor;
    
    // 3. 从旋转矩阵提取欧拉角
    // 注意：toRotationMatrix()创建正交矩阵，可以直接提取欧拉角
    Quaternionf body_quat(R_body);
    return quaternionToEuler(body_quat);
}

// 获取当前校准状态
bool BodyFrameTracker::isCalibrated() const {
    return calibrated;
}

// 重置校准
void BodyFrameTracker::resetCalibration() {
    calibrated = false;
    accel_buffer.clear();
}


/*
    无论传感器如何放置在PCB上，以及PCB如何固定在身体上（比如口袋中），
    我们都要将其转换到一个标准位置（例如：PCB截面与大腿侧边平行，正面
    朝外等）的人体坐标系。
*/


