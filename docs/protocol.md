# Canonical Protocol v1‑Lite (修订版)  
**可穿戴设备传感器数据通信协议文档**  
*版本：v1‑Lite (2024‑03‑20)*  

---

## 📋 文档变更记录

| 版本 | 日期 | 作者 | 说明 |
|------|------|------|------|
| v1‑Lite | 2024‑03‑20 | 泊宇运动科技 | 初始版本，精简设计 |

---

## 一、设计原则

1. **V1‑Lite 原则**  
   - 帧结构完整不动，保留未来扩展能力  
   - 只定义**最小必需的 TLV 集合**  
   - 不引入复杂结构（无嵌套、无位域）  
   - 字节级定义，可直接实现代码  

2. **当前需求覆盖**  
   ✅ 添加：爬升高度、电量、运动类型、运动类型概率、总运动时间  
   ✅ TLV 数量尽量少（当前 5 个）  

---

## 二、帧格式 (完整定义)

### 2.1 帧布局 (小端序)

| 字节偏移 | 字段 | 长度 | 类型 | 值 | 说明 |
|----------|------|------|------|-----|------|
| 0 | SOF0 | 1 | uint8 | `0xAA` | 帧起始符 0 |
| 1 | SOF1 | 1 | uint8 | `0x55` | 帧起始符 1 |
| 2 | VER | 1 | uint8 | `0x01` | 协议版本 1 |
| 3 | TYPE | 1 | uint8 | 见类型表 | 消息类型 |
| 4 | FLAGS | 1 | uint8 | 位域 | 标志位 |
| 5 | SEQ | 1 | uint8 | 0‑255 | 序列号 (循环) |
| 6 | LEN低字节 | 1 | uint8 | - | 载荷长度低字节 |
| 7 | LEN高字节 | 1 | uint8 | - | 载荷长度高字节 |
| 8..(8+LEN‑1) | PAYLOAD | LEN | bytes | TLV 序列 | 数据载荷 |
| 8+LEN.. | CRC低字节 | 1 | uint8 | - | CRC16 低字节 |
| 9+LEN.. | CRC高字节 | 1 | uint8 | - | CRC16 高字节 |

**最小帧长度**：10 字节 (当 LEN = 0)  
**CRC 覆盖范围**：从 `VER` 到 `PAYLOAD` 结束  
**CRC 算法**：CRC16‑CCITT‑FALSE (Poly = 0x1021, 初始值 = 0xFFFF)

### 2.2 TYPE (消息类型)

| 类型值 | 名称 | 方向 | 用途 |
|--------|------|------|------|
| 0x01 | HELLO | 双向 | 连接建立/能力协商 |
| 0x02 | STATE | 节点→主机 | 当前运动状态 |
| 0x03 | TELEMETRY | 节点→主机 | 周期性传感器数据 |
| 0x05 | CONTROL | 主机→节点 | 控制命令 |
| 0x7F | ACK | 双向 | 确认响应 |

### 2.3 FLAGS (标志位)

| Bit 位 | 名称 | 含义 | V1‑Lite 使用规则 |
|--------|------|------|-------------------|
| 0 | ENCRYPTED | 加密帧 | 固定 0 (未加密) |
| 1 | COMPRESSED | 压缩帧 | 固定 0 (未压缩) |
| 2 | ACK_REQ | 需要确认 | ✅ 可启用 |
| 3 | HAS_TS | 含时间戳 | ✅ 若 TIMESTAMP_MS TLV 存在则置 1 |
| 4 | FRAG | 分片帧 | 固定 0 |
| 5‑7 | RESERVED | 保留 | 必须为 0 |

---

## 三、Payload TLV 定义 (当前版本)

> ⚠️ **V1‑Lite 规则**：仅解析下列 TLV，其他 TLV 一律忽略  

### TLV 通用格式

| 偏移 | 字段 | 长度 | 说明 |
|------|------|------|------|
| 0 | T (Type) | 1 | TLV 类型码 |
| 1 | L (Length) | 1 | Value 长度 (0‑255) |
| 2..(2+L‑1) | V (Value) | L | 数据值 |

---

### 3.1 0x01 TIMESTAMP_MS (时间戳)

| 字节 | 字段 | 类型 | 单位/范围 | 说明 |
|------|------|------|-----------|------|
| 0‑3 | ts_ms | uint32 LE | 毫秒 | 设备启动后的单调时间 |

**规则**：
- L = 4
- 若存在此 TLV → `FLAGS.HAS_TS = 1`
- 时钟源：系统时钟 (上电/开始工作时清零)
- 范围：0x00000000 ~ 0xFFFFFFFF (约 49.7 天)

---

### 3.2 0x02 MOTION_TYPE (运动类型与概率)

| 字节 | 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|------|
| 0 | type | uint8 | 0‑255 | 运动类型枚举 |
| 1 | probability | uint8 | 0‑100 | 置信度百分比 |

**枚举定义**：
| type | 名称 | 说明 |
|------|------|------|
| 0 | UNKNOWN | 未知状态 |
| 1 | STILL | 静止 |
| 2 | WALK | 步行 |
| 3 | RUN | 跑步 |
| 4 | CYCLE | 骑行 |
| 5 | CLIMB | 爬升/登山 |
| 6 | OTHER | 其他活动 |

**规则**：
- L = 2
- probability = 0‑100 (0=完全不确信, 100=完全确信)
- **原子绑定**：type 与 probability 必须在同一个 TLV 中
- 新增运动类型：仅添加新枚举值，不修改现有语义

---

### 3.3 0x03 MOTION_DURATION (持续运动时间)

| 字节 | 字段 | 类型 | 单位/范围 | 说明 |
|------|------|------|-----------|------|
| 0‑3 | duration_s | uint32 LE | 秒 | 当前运动类型持续时长 |

**语义规则**：
- L = 4
- 单位为**秒**
- 指**当前运动的连续持续时间**
- **重置条件**：
  1. 运动类型改变
  2. 运动中断超过预设阈值 (例如 2 秒)
- 范围：0‑4294967295 秒 (约 136 年)

---

### 3.4 0x04 ASCENT_HEIGHT (累计爬升高度)

| 字节 | 字段 | 类型 | 单位/范围 | 说明 |
|------|------|------|-----------|------|
| 0‑1 | ascent_m | int16 LE | 米 | 累计爬升高度 |

**规则**：
- L = 2
- 单位：**米**
- 支持负值 (算法修正时可出现)
- 范围：‑32768 m ∼ +32767 m
- 精度：如需更高精度，可采用 0.1 米为单位 (文档约定即可)

---

### 3.5 0x05 BATTERY_PERCENT (电量百分比)

| 字节 | 字段 | 类型 | 范围 | 说明 |
|------|------|------|------|------|
| 0 | percent | uint8 | 0‑100 | 电量百分比 |

**特殊值**：
- 255 (0xFF)：未知电量
- 0‑100：实际电量百分比

---

## 四、消息类型与 TLV 组合规则

### 4.1 STATE (TYPE = 0x02)

**MUST (必须包含)**：
- MOTION_TYPE (0x02)
- MOTION_DURATION (0x03)

**MAY (可选包含)**：
- TIMESTAMP_MS (0x01)

**说明**：
- 用于传输运动状态变化
- 频率：状态变化时或定期上报 (如 1 Hz)
- 可不含其他传感器数据

---

### 4.2 TELEMETRY (TYPE = 0x03)

**允许包含任意组合**：
- ASCENT_HEIGHT (0x04)
- BATTERY_PERCENT (0x05)
- TIMESTAMP_MS (0x01)

**说明**：
- 用于周期性传感器数据上报
- 频率：较低 (如 0.1‑0.5 Hz)
- **可以**同时包含 MOTION_TYPE，但非必须

---

## 五、数据包大小分析

### 5.1 字节数计算

**固定帧开销** (每帧必有)：
```
SOF(2) + VER(1) + TYPE(1) + FLAGS(1) + SEQ(1) + LEN(2) + CRC(2) = 10 字节
```

**各 TLV 开销**：
| TLV | 头(T+L) | 数据(V) | 合计 |
|-----|---------|---------|------|
| TIMESTAMP_MS | 2 | 4 | 6 字节 |
| MOTION_TYPE | 2 | 2 | 4 字节 |
| MOTION_DURATION | 2 | 4 | 6 字节 |
| ASCENT_HEIGHT | 2 | 2 | 4 字节 |
| BATTERY_PERCENT | 2 | 1 | 3 字节 |

### 5.2 典型场景包大小

| 场景 | 组合 | Payload 长度 | 总包大小 |
|------|------|--------------|----------|
| 最小 STATE | MOTION_TYPE + DURATION (4+6) | 10 字节 | 20 字节 |
| 完整 TELEMETRY | 全部 5 个 TLV | 23 字节 | 33 字节 |
| 常规 TELEMETRY | 除 TIMESTAMP 外全部 | 17 字节 | 27 字节 |

### 5.3 BLE 传输适配性

| BLE MTU | 可用载荷 | 典型包表现 |
|---------|----------|------------|
| 23 (默认) | ~20 字节 | STATE 可单包，TELEMETRY 需 2 包 |
| 64 | ~61 字节 | 所有组合单包 |
| 128 | ~125 字节 | 所有组合单包 |

**结论**：30‑35 字节对于 BLE 属于**偏小、完全可接受**范围。

---

## 六、编码示例

### 6.1 完整 TELEMETRY 示例

**场景**：
- 运动：CLIMB (5)
- 置信度：82%
- 持续时间：135 秒
- 累计爬升：+42 米
- 电量：88%
- 时间戳：1000 毫秒

**Payload (TLV 序列)**：
```
01 04 E8 03 00 00   // TIMESTAMP_MS = 1000 (0x3E8)
02 02 05 52         // MOTION_TYPE = CLIMB(5), prob=82(0x52)
03 04 87 00 00 00   // MOTION_DURATION = 135秒(0x87)
04 02 2A 00         // ASCENT_HEIGHT = +42米(0x2A)
05 01 58            // BATTERY = 88%(0x58)
```

**帧参数计算**：
- Payload 长度 = 6+4+6+4+3 = 23 字节 (0x0017)
- FLAGS = HAS_TS → `0x08`
- TYPE = TELEMETRY → `0x03`
- SEQ (假设) → `0x22`
- CRC 需计算

**完整帧 (不含 CRC)**：
```
AA 55 01 03 08 22 17 00   // 帧头
01 04 E8 03 00 00         // TLV1: TIMESTAMP
02 02 05 52               // TLV2: MOTION_TYPE
03 04 87 00 00 00         // TLV3: MOTION_DURATION
04 02 2A 00               // TLV4: ASCENT_HEIGHT
05 01 58                  // TLV5: BATTERY
[CRC低字节 CRC高字节]     // CRC字段
```

---

## 七、实现指南

### 7.1 编码建议 (C 语言示例)
```c
// TLV 结构体 (紧凑)
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t len;
    uint8_t value[];
} tlv_t;

// 帧结构体
typedef struct __attribute__((packed)) {
    uint8_t sof[2];      // 0xAA, 0x55
    uint8_t ver;         // 0x01
    uint8_t type;        // TYPE_*
    uint8_t flags;       // FLAGS位域
    uint8_t seq;         // 序列号
    uint16_t len;        // 载荷长度
    uint8_t payload[];   // TLV序列
    // CRC16 在发送时追加
} frame_t;
```

### 7.2 解码流程
1. 检测 SOF (0xAA55)
2. 验证 CRC16
3. 检查 VER = 0x01
4. 根据 TYPE 处理
5. 循环解析 TLV，直到 payload 结束

---

## 八、扩展策略 (留白设计)

### 8.1 保留位/值
- **TYPE 保留**：0x04, 0x06‑0x7E (除 0x04 EVENT, 0x05 CONTROL, 0x7F ACK)
- **TLV 类型保留**：0x06‑0xDF (用户扩展), 0xE0‑0xFF (系统扩展)
- **FLAGS 保留**：Bit 5‑7

### 8.2 未来可扩展字段
1. **更高精度高度**：TLV 0x24 (float32) 或 0x25 (int32 cm)
2. **IMU 数据**：TLV 0x40‑0x4F (加速度/陀螺仪/磁力计)
3. **GPS 数据**：TLV 0x50‑0x5F (经纬度/速度/航向)
4. **事件系统**：TYPE = 0x04 EVENT

### 8.3 兼容性保证
- **V1 到 V2**：仅新增字段，不删除/修改现有字段语义
- **旧主机 + 新设备**：忽略未知 TLV
- **新主机 + 旧设备**：缺失 TLV 按默认值处理

---

## 九、附录

### 9.1 预定义常量表
```c
// TYPE
#define TYPE_HELLO       0x01
#define TYPE_STATE       0x02
#define TYPE_TELEMETRY   0x03
#define TYPE_CONTROL     0x05
#define TYPE_ACK         0x7F

// TLV Type
#define TLV_TIMESTAMP    0x01
#define TLV_MOTION_TYPE  0x02
#define TLV_MOTION_DUR   0x03
#define TLV_ASCENT_HEIGHT 0x04
#define TLV_BATTERY      0x05

// Motion Type
#define MOTION_UNKNOWN   0
#define MOTION_STILL     1
#define MOTION_WALK      2
#define MOTION_RUN       3
#define MOTION_CYCLE     4
#define MOTION_CLIMB     5
#define MOTION_OTHER     6

// FLAGS bits
#define FLAG_ENCRYPTED   (1 << 0)
#define FLAG_COMPRESSED  (1 << 1)
#define FLAG_ACK_REQ     (1 << 2)
#define FLAG_HAS_TS      (1 << 3)
#define FLAG_FRAG        (1 << 4)
```

### 9.2 测试向量
| 字段 | 测试值 | 编码 (小端) |
|------|--------|-------------|
| 时间戳 1000ms | 0x000003E8 | `E8 03 00 00` |
| 运动类型 WALK (2) | type=2, prob=75 | `02 4B` |
| 持续时间 300s | 0x0000012C | `2C 01 00 00` |
| 爬升高度 +15m | 15 | `0F 00` |
| 电量 60% | 60 | `3C` |

---

## 📄 文档状态
- ✅ **已完成**：协议结构、TLV 定义、示例
- 🔄 **进行中**：各平台 SDK 实现
- 📅 **计划**：测试规范、压力测试

*文档版本：v1‑Lite‑20240320*  
*适用于：可穿戴设备传感器数据通信*  
*维护团队：泊宇运动科技*