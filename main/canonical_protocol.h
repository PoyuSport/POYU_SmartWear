#pragma once

#include <stdint.h>
#include <string.h>
#include <vector>
#include <functional>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#endif

namespace CanonicalProtocol {

// ============================
// 常量定义
// ============================

// Frame 常量
static const uint8_t SOF0_VALUE = 0xAA;
static const uint8_t SOF1_VALUE = 0x55;
static const uint8_t PROTOCOL_VERSION = 0x01;
static const uint16_t MIN_FRAME_SIZE = 10;  // 无payload时的最小帧大小

// TYPE 枚举
enum class FrameType : uint8_t {
    HELLO       = 0x01,
    STATE       = 0x02,
    TELEMETRY   = 0x03,
    CONTROL     = 0x05,
    ACK         = 0x7F
};

// FLAGS 位定义
enum FrameFlag : uint8_t {
    FLAG_ENCRYPTED  = (1 << 0),
    FLAG_COMPRESSED = (1 << 1),
    FLAG_ACK_REQ    = (1 << 2),
    FLAG_HAS_TS     = (1 << 3),
    FLAG_FRAG       = (1 << 4)
};

// TLV Type 枚举
enum class TLVType : uint8_t {
    TIMESTAMP_MS        = 0x01,
    MOTION_TYPE         = 0x02,
    MOTION_DURATION     = 0x03,
    ASCENT_HEIGHT       = 0x04,
    BATTERY_PERCENT     = 0x05,
    BAROMETRIC_PRESSURE = 0x06,   
    BATTERY_SOC         = 0x07,
    SPORT_LAPS          = 0x08,
    RPM                 = 0x09,
    STATE_PROB          = 0x0A,
};

// Motion Type 枚举
enum class MotionType : uint8_t {
    UNKNOWN = 0,
    WALK    = 1,
    CYCLE   = 2,
    RUN     = 3,
    STILL   = 4,
    CLIMB   = 5,
    OTHER   = 6
};

// ============================
// 数据结构定义
// ============================

#pragma pack(push, 1)

// TLV 结构
struct TLV {
    uint8_t type;
    uint8_t length;
    uint8_t value[];
    
    // 返回完整大小
    uint16_t totalSize() const { return 2 + length; }
    
    // 获取value指针
    uint8_t* getValuePtr() { return value; }
    const uint8_t* getValuePtr() const { return value; }
};

// Frame 结构 (用于解析)
struct Frame {
    uint8_t sof[2];     // 0: SOF0, 1: SOF1
    uint8_t version;    // VER
    uint8_t type;       // TYPE
    uint8_t flags;      // FLAGS
    uint8_t seq;        // SEQ
    uint16_t length;    // LEN (小端)
    uint8_t payload[];
    // CRC16 在最后，动态处理
    
    // 检查SOF
    bool hasValidSOF() const { return sof[0] == SOF0_VALUE && sof[1] == SOF1_VALUE; }
    
    // 获取CRC位置
    uint16_t* getCRCPtr() { return reinterpret_cast<uint16_t*>(&payload[length]); }
    const uint16_t* getCRCPtr() const { return reinterpret_cast<const uint16_t*>(&payload[length]); }
    
    // 获取帧完整大小
    uint16_t totalSize() const { return 8 + length + 2; }  // 前8字节 + payload + CRC
};

#pragma pack(pop)

// ============================
// 数据容器结构
// ============================

struct TimestampData {
    uint32_t timestamp_ms = 0;  // 设备启动后的毫秒数
};

struct MotionTypeData {
    MotionType type = MotionType::UNKNOWN;
    uint8_t probability = 0;  // 置信度 0-100
};

struct MotionDurationData {
    uint32_t duration_s = 0;  // 当前运动持续秒数
};

struct AscentHeightData {
    int16_t ascent_m = 0;  // 累计爬升高度(米)
};

struct BatteryData {
    uint8_t percent = 0;  // 电量百分比
};

struct BarometricPressureData {
    uint32_t altitude_m = 0;  // 海拔高度
};

// RPM 数据
struct RPMData {
    uint32_t RPM = 0;  // 转速
};

// 运动圈数数据
struct SportLapsData {
    uint32_t laps = 0;  // 运动圈数
};

// 完整数据包
struct PacketData {
    FrameType frame_type = FrameType::STATE;
    uint8_t flags = 0;
    uint8_t seq = 0;
    
    TimestampData timestamp;
    MotionTypeData motion_type;
    MotionDurationData motion_duration;
    AscentHeightData ascent_height;
    BatteryData battery;
    BarometricPressureData altitude;
    RPMData rpm;
    SportLapsData sport_laps;
    
    bool has_timestamp = false;
    bool has_motion_type = false;
    bool has_motion_duration = false;
    bool has_ascent_height = false;
    bool has_battery = false;
    bool has_altitude = false;
    bool has_rpm = false;
    bool has_sport_laps = false;
    
    void reset() {
        *this = PacketData();
    }
    
    void setType(FrameType type) {
        frame_type = type;
    }
    
    void setTimestamp(uint32_t ts_ms) {
        timestamp.timestamp_ms = ts_ms;
        has_timestamp = true;
        flags |= FLAG_HAS_TS;
    }
    
    void setMotionType(MotionType type, uint8_t prob) {
        motion_type.type = type;
        motion_type.probability = prob;
        has_motion_type = true;
    }
    
    void setMotionDuration(uint32_t duration_s) {
        motion_duration.duration_s = duration_s;
        has_motion_duration = true;
    }
    
    void setAscentHeight(int16_t ascent_m) {
        ascent_height.ascent_m = ascent_m;
        has_ascent_height = true;
    }
    
    void setBattery(uint8_t percent) {
        battery.percent = (percent > 100) ? 100 : percent;
        has_battery = true;
    }
    
    void setAltitude(uint32_t altitude_m) {
        altitude.altitude_m = altitude_m;
        has_altitude = true;
    }

    void setRPM(uint32_t rpm_value){
        rpm.RPM = rpm_value;
        has_rpm = true;
    }

    void setSportLaps(uint32_t sportLaps){
        sport_laps.laps = sportLaps;
        has_sport_laps = true;
    }
    
    // 为STATE帧准备数据
    void prepareStatePacket() {
        setType(FrameType::STATE);
        // STATE必须有运动类型和持续时间
        if (!has_motion_type) {
            setMotionType(MotionType::STILL, 100);
        }
        if (!has_motion_duration) {
            setMotionDuration(0);
        }
    }
    
    // 为TELEMETRY帧准备数据
    void prepareTelemetryPacket() {
        setType(FrameType::TELEMETRY);
    }
};

// ============================
// CRC16-CCITT-FALSE 计算
// ============================

class CRC16 {
private:
    static const uint16_t POLYNOMIAL = 0x1021;
    static const uint16_t INIT_VALUE = 0xFFFF;
    
    static uint16_t crc16_update(uint16_t crc, uint8_t data) {
        crc ^= (uint16_t)data << 8;
        for (int i = 0; i < 8; i++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
        return crc;
    }
    
public:
    static uint16_t calculate(const uint8_t* data, size_t length) {
        uint16_t crc = INIT_VALUE;
        for (size_t i = 0; i < length; i++) {
            crc = crc16_update(crc, data[i]);
        }
        return crc;
    }
    
    static bool verify(const uint8_t* data, size_t length, uint16_t expected_crc) {
        return calculate(data, length) == expected_crc;
    }
};

// ============================
// 协议编码器
// ============================

class Encoder {
private:
    uint8_t seq_counter = 0;
    
    // 增加序列号
    uint8_t getNextSeq() {
        return seq_counter++;
    }
    
    // 添加TLV到缓冲区
    bool addTLV(std::vector<uint8_t>& buffer, TLVType type, const uint8_t* value, uint8_t length) {
        if (buffer.size() + 2 + length > 0xFFFF) return false;  // payload长度限制
        
        buffer.push_back(static_cast<uint8_t>(type));
        buffer.push_back(length);
        buffer.insert(buffer.end(), value, value + length);
        return true;
    }
    
    // 编码时间戳
    bool encodeTimestamp(std::vector<uint8_t>& buffer, uint32_t timestamp_ms) {
        uint8_t data[4];
        data[0] = timestamp_ms & 0xFF;
        data[1] = (timestamp_ms >> 8) & 0xFF;
        data[2] = (timestamp_ms >> 16) & 0xFF;
        data[3] = (timestamp_ms >> 24) & 0xFF;
        return addTLV(buffer, TLVType::TIMESTAMP_MS, data, 4);
    }
    
    // 编码运动类型
    bool encodeMotionType(std::vector<uint8_t>& buffer, MotionType type, uint8_t probability) {
        uint8_t data[2] = {
            static_cast<uint8_t>(type),
            probability
        };
        return addTLV(buffer, TLVType::MOTION_TYPE, data, 2);
    }
    
    // 编码运动持续时间
    bool encodeMotionDuration(std::vector<uint8_t>& buffer, uint32_t duration_s) {
        uint8_t data[4];
        data[0] = duration_s & 0xFF;
        data[1] = (duration_s >> 8) & 0xFF;
        data[2] = (duration_s >> 16) & 0xFF;
        data[3] = (duration_s >> 24) & 0xFF;
        return addTLV(buffer, TLVType::MOTION_DURATION, data, 4);
    }
    
    // 编码爬升高度
    bool encodeAscentHeight(std::vector<uint8_t>& buffer, int16_t ascent_m) {
        uint8_t data[2];
        data[0] = ascent_m & 0xFF;
        data[1] = (ascent_m >> 8) & 0xFF;
        return addTLV(buffer, TLVType::ASCENT_HEIGHT, data, 2);
    }
    
    // 编码电量
    bool encodeBattery(std::vector<uint8_t>& buffer, uint8_t percent) {
        return addTLV(buffer, TLVType::BATTERY_PERCENT, &percent, 1);
    }
    
    // 编码海拔高度
    bool encodeAltitude(std::vector<uint8_t>& buffer, uint32_t altitude) {
        uint8_t data[4];
        data[0] = altitude & 0xFF;
        data[1] = (altitude >> 8) & 0xFF;
        data[2] = (altitude >> 16) & 0xFF;
        data[3] = (altitude >> 24) & 0xFF;
        return addTLV(buffer, TLVType::BAROMETRIC_PRESSURE, data, 4);
    }

    // RPM
    bool encodeRPM(std::vector<uint8_t>& buf, uint32_t rpm)
    {
        uint8_t d[4];
        d[0] = rpm & 0xFF;
        d[1] = (rpm >> 8) & 0xFF;
        d[2] = (rpm >> 16) & 0xFF;
        d[3] = (rpm >> 24) & 0xFF;
        return addTLV(buf, TLVType::RPM, d, 4);
    }

    bool encodeSportLaps(std::vector<uint8_t>& buf, uint32_t laps)
    {
        uint8_t d[4];
        d[0] = laps & 0xFF;
        d[1] = (laps >> 8) & 0xFF;
        d[2] = (laps >> 16) & 0xFF;
        d[3] = (laps >> 24) & 0xFF;
        return addTLV(buf, TLVType::SPORT_LAPS, d, 4);
    }

    
public:
    // 编码完整包
    std::vector<uint8_t> encodePacket(const PacketData& data, bool ack_request = false) {
        std::vector<uint8_t> buffer;
        std::vector<uint8_t> payload;
        
        uint8_t flags = data.flags;
        if (ack_request) {
            flags |= FLAG_ACK_REQ;
        }
        
        // 1. 构建payload
        if (data.has_timestamp) {
            encodeTimestamp(payload, data.timestamp.timestamp_ms);
        }
        
        if (data.has_motion_type) {
            encodeMotionType(payload, data.motion_type.type, data.motion_type.probability);
        }
        
        if (data.has_motion_duration) {
            encodeMotionDuration(payload, data.motion_duration.duration_s);
        }
        
        if (data.has_ascent_height) {
            encodeAscentHeight(payload, data.ascent_height.ascent_m);
        }
        
        if (data.has_battery) {
            encodeBattery(payload, data.battery.percent);
        }
        
        if (data.has_altitude) {
            encodeAltitude(payload, data.altitude.altitude_m);
        }
        
        if(data.has_rpm){
            encodeRPM(payload, data.rpm.RPM); 
        }

        if(data.has_sport_laps){
            encodeSportLaps(payload, data.sport_laps.laps);
        }

        // 2. 构建帧头
        buffer.reserve(8 + payload.size() + 2);
        
        // SOF
        buffer.push_back(SOF0_VALUE);
        buffer.push_back(SOF1_VALUE);
        
        // VER
        buffer.push_back(PROTOCOL_VERSION);
        
        // TYPE
        buffer.push_back(static_cast<uint8_t>(data.frame_type));
        
        // FLAGS
        buffer.push_back(flags);
        
        // SEQ
        uint8_t seq = (data.seq == 0) ? getNextSeq() : data.seq;
        buffer.push_back(seq);
        
        // LEN (小端)
        uint16_t payload_len = payload.size();
        buffer.push_back(payload_len & 0xFF);
        buffer.push_back((payload_len >> 8) & 0xFF);
        
        // 3. 添加payload
        buffer.insert(buffer.end(), payload.begin(), payload.end());
        
        // 4. 计算并添加CRC
        // CRC覆盖范围: VER到payload结束
        const uint8_t* crc_start = &buffer[2];  // VER的位置
        uint16_t crc_len = buffer.size() - 2;   // 从VER到当前末尾
        uint16_t crc = CRC16::calculate(crc_start, crc_len);
        
        buffer.push_back(crc & 0xFF);       // CRC低字节
        buffer.push_back((crc >> 8) & 0xFF); // CRC高字节
        
        return buffer;
    }
    
    // 快速编码HELLO包
    std::vector<uint8_t> encodeHello() {
        PacketData data;
        data.setType(FrameType::HELLO);
        data.setTimestamp(esp_timer_get_time() / 1000);  // 使用ESP32计时器
        return encodePacket(data);
    }
    
    // 快速编码ACK包
    std::vector<uint8_t> encodeAck(uint8_t seq_to_ack) {
        std::vector<uint8_t> buffer;
        buffer.reserve(10);  // 最小帧
        
        // SOF
        buffer.push_back(SOF0_VALUE);
        buffer.push_back(SOF1_VALUE);
        
        // VER
        buffer.push_back(PROTOCOL_VERSION);
        
        // TYPE
        buffer.push_back(static_cast<uint8_t>(FrameType::ACK));
        
        // FLAGS
        buffer.push_back(0);  // 无特殊flags
        
        // SEQ
        buffer.push_back(seq_to_ack);
        
        // LEN = 0
        buffer.push_back(0);
        buffer.push_back(0);
        
        // CRC
        const uint8_t* crc_start = &buffer[2];
        uint16_t crc = CRC16::calculate(crc_start, 6);  // VER到LEN
        buffer.push_back(crc & 0xFF);
        buffer.push_back((crc >> 8) & 0xFF);
        
        return buffer;
    }
};

// ============================
// 协议解码器
// ============================

class Decoder {
private:
    enum class ParseState {
        IDLE,
        SOF0_FOUND,
        SOF1_FOUND,
        READING_HEADER,
        READING_PAYLOAD,
        READING_CRC,
        COMPLETE
    };
    
    ParseState state = ParseState::IDLE;
    std::vector<uint8_t> buffer;
    size_t expected_length = 0;
    uint16_t payload_length = 0;
    
    // 重置解析状态
    void reset() {
        state = ParseState::IDLE;
        buffer.clear();
        expected_length = 0;
        payload_length = 0;
    }
    
    // 解析TLV
    bool parseTLV(const TLV* tlv, PacketData& data) {
        TLVType type = static_cast<TLVType>(tlv->type);
        
        switch (type) {
            case TLVType::TIMESTAMP_MS: {
                if (tlv->length != 4) return false;
                uint32_t ts = 
                    tlv->value[0] |
                    (tlv->value[1] << 8) |
                    (tlv->value[2] << 16) |
                    (tlv->value[3] << 24);
                data.setTimestamp(ts);
                break;
            }
            
            case TLVType::MOTION_TYPE: {
                if (tlv->length != 2) return false;
                MotionType motion_type = static_cast<MotionType>(tlv->value[0]);
                uint8_t probability = tlv->value[1];
                data.setMotionType(motion_type, probability);
                break;
            }
            
            case TLVType::MOTION_DURATION: {
                if (tlv->length != 4) return false;
                uint32_t duration = 
                    tlv->value[0] |
                    (tlv->value[1] << 8) |
                    (tlv->value[2] << 16) |
                    (tlv->value[3] << 24);
                data.setMotionDuration(duration);
                break;
            }
            
            case TLVType::ASCENT_HEIGHT: {
                if (tlv->length != 2) return false;

                int16_t height =
                    tlv->value[0] |
                    (tlv->value[1] << 8);

                data.setAscentHeight(height);
                break;
            }

            case TLVType::BATTERY_PERCENT: {
                if (tlv->length != 1) return false;
                data.setBattery(tlv->value[0]);
                break;
            }

            case TLVType::BAROMETRIC_PRESSURE: {
                if (tlv->length != 4) return false;
                uint32_t altitude =
                    tlv->value[0] |
                    (tlv->value[1] << 8) |
                    (tlv->value[2] << 16) |
                    (tlv->value[3] << 24);
                data.setAltitude(altitude);
                break;
            }

            case TLVType::SPORT_LAPS: {
                if (tlv->length != 4) return false;
                uint32_t laps =
                    tlv->value[0] |
                    (tlv->value[1] << 8) |
                    (tlv->value[2] << 16) |
                    (tlv->value[3] << 24);
                data.setSportLaps(laps);
                break;
            }

            case TLVType::RPM: {
                if (tlv->length != 4) return false;
                uint32_t rpm =
                    tlv->value[0] |
                    (tlv->value[1] << 8) |
                    (tlv->value[2] << 16) |
                    (tlv->value[3] << 24);
                data.setRPM(rpm);
                break;
            }

            default:
                // V1‑Lite 规则：未知 TLV 直接忽略
                break;
        }
        return true;
    }

public:
    /**
     * @brief 解析完整帧
     * @param raw 原始字节流
     * @param length 数据长度
     * @param out_data 解析后的 PacketData
     * @return true 成功解析
     */
    bool decodeFrame(const uint8_t* raw, size_t length, PacketData& out_data) {
        if (length < MIN_FRAME_SIZE) {
            return false;
        }

        const Frame* frame = reinterpret_cast<const Frame*>(raw);

        // 1. 校验 SOF
        if (!frame->hasValidSOF()) {
            return false;
        }

        // 2. 校验版本
        if (frame->version != PROTOCOL_VERSION) {
            return false;
        }

        // 3. 校验长度
        uint16_t payload_len = frame->length;
        uint16_t expected_len = 8 + payload_len + 2;
        if (length < expected_len) {
            return false;
        }

        // 4. 校验 CRC
        const uint8_t* crc_start = &raw[2]; // 从 VER 开始
        uint16_t crc_len = 6 + payload_len;
        uint16_t recv_crc =
            raw[8 + payload_len] |
            (raw[9 + payload_len] << 8);

        uint16_t calc_crc = CRC16::calculate(crc_start, crc_len);
        if (recv_crc != calc_crc) {
            return false;
        }

        // 5. 填充基础信息
        out_data.reset();
        out_data.frame_type = static_cast<FrameType>(frame->type);
        out_data.flags = frame->flags;
        out_data.seq = frame->seq;

        // 6. 解析 TLV
        const uint8_t* p = frame->payload;
        const uint8_t* end = p + payload_len;

        while (p + 2 <= end) {
            const TLV* tlv = reinterpret_cast<const TLV*>(p);
            if (p + tlv->totalSize() > end) {
                return false;
            }

            parseTLV(tlv, out_data);
            p += tlv->totalSize();
        }

        return true;
    }
};

} 