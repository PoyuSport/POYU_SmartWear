# Smart Wear · ESP32-S3 端侧运动识别

这是一个可运行的 ESP32-S3 可穿戴固件与训练管线：采集 IMU / 气压计 / 电量等数据，在设备端用 TensorFlow Lite Micro 做 **4 类运动识别**（其他 / 步行 / 骑行 / 跑步），通过 BLE 上报状态，也支持 Wi-Fi AP + WebSocket 调试。

> 这是 **开源版本**：包含固件、模型、训练脚本，以及 ESP32-S3 的 Altium 硬件工程（`hardware/`）。
> 原始采集数据、数据手册、厂商安装包和构建产物 **不在本仓库中**。

## 功能总览

### 设备与固件

- **ESP32-S3-WROOM-1**，16 MB Flash，Octal PSRAM 80 MHz；ESP-IDF 5.3.1 + Arduino-ESP32 3.3.1
- **传感器驱动**：ICM-20948、MPU-9250、SC7A22H、MS5611 气压计、MAX17043 电量计、EEPROM、microSD
- **传感器融合**：四元数滤波、BodyFrameTracker、Butterworth 滤波、气压高度补偿、RPM/圈数
- **双模式**：`ENABLE_DEDUCTION_FLAG=0` 只采集，`=1` 端侧推理
- **数据记录**：50 Hz CSV 写 SD 卡，文件轮转；包含姿态、加速度、角速度、四元数、RPM、类型、时间
- **低功耗**：自动 light sleep、DFS、Wi-Fi modem sleep、深度睡眠、GPIO ext0/ext1 唤醒
- **状态持久化**：NVS 保存爬升高度、圈数、海平面气压、文件计数、启动次数和时间恢复
- **可靠性**：启动原因、复位统计、诊断日志

### 端侧 AI

- **4 类识别**：其他 / 步行 / 骑行 / 跑步；输出概率，低于 0.4 归为“未知”
- **输入**：12 维特征 × 75 帧窗口（50 Hz 约 1.5 秒），步长 40 帧
- **模型**：Conv1D + LSTM + 均值/标准差分支 → Dense64 → 4 类 softmax
- **推理框架**：TFLite Micro，float32 输入 + int8 算子；仓库同时保留 int8/float32 两套模型头文件
- **内存**：256 KB PSRAM tensor arena，27 个注册算子，16 字节对齐
- **流水线**：50 帧为一块写入 PSRAM 双缓冲，推理时拷贝模型需要的 75×12 输入
- **模型体积**：约 629 KB，C 数组直接链接进固件，不依赖文件系统

### 通信与调试

- **BLE**：广播名 `POYUSPORT`，Service `4fafc201-…`，Characteristic `beb5483e-…`
- **TLV 协议**：HELLO / STATE / TELEMETRY / CONTROL / ACK，CRC16-CCITT-FALSE
- **兼容文本行**：`poyu#...`，便于旧工具或串口调试
- **Wi-Fi**：SoftAP + WebSocket 实时数据调试
- **串口日志**：tensor arena 占用、推理类别/概率、复位原因
- **测试前端**：微信小程序 **Poyu Sport**；也可用 nRF Connect / LightBlue

### 训练与工具链

- 采集 → 检查 → 标注 → 窗口化 → 训练 → 剪枝 → 量化 → TFLite → C 头文件
- 按 `Sample_Block` 分组划分，避免同一段窗口泄漏到测试集
- 剪枝 5%→10%，int8 量化，representative dataset 抽 500 个窗口
- `training/tools/`：数据统计、CSV 拆分、带通滤波、TFLite 输入/量化检查
- `eval_tflite.py`：准确率、混淆矩阵、逐样本对比
- 完整方法见 [docs/TRAINING.md](docs/TRAINING.md)

### 硬件与结构

- Altium `版本18_four`：原理图、PCB、封装库
- 实物照片、PCB 顶层/底层版图
- 外壳正面/反面 STL，以及装配、外部、内部渲染
- 硬件设计使用 CERN-OHL-W-2.0

### 文档与许可

- 协议、编译、数据格式、训练流程、开发环境、小程序测试说明
- 原创代码 Apache-2.0；第三方组件保留各自许可；模型 Apache-2.0；硬件 CERN-OHL-W-2.0

## 值得拿出来说的功能

| 功能 | 用户能做什么 |
|---|---|
| **口袋里的运动识别** | 把板子放进口袋，设备端直接判断“其他 / 步行 / 骑行 / 跑步”，通过蓝牙把状态和概率发到手机，不依赖云端 |
| **不确定就不猜** | 最高概率低于 0.4 时显示“未知”，不把模棱两可的动作硬分到某一类 |
| **一块板子两种用途** | 采集模式当数据记录器，推理模式当识别器，改一个开关即可切换 |
| **一份完整的运动记录** | 姿态、加速度、角速度、四元数、气压高度、爬升、电量、RPM/圈数、类型和时间都写进记录文件 |
| **长时间口袋佩戴** | 自动休眠，运动或按键唤醒，适合断断续续记录一整天 |
| **不用拆机看波形** | 板子开热点，手机或电脑浏览器就能看实时数据，方便调传感器和算法 |
| **采完就能训练** | 同一份记录文件直接进训练脚本；仓库自带数据检查、拆分、滤波、模型检查工具 |
| **照着能做一台** | 原理图、PCB、封装库、外壳正面/反面 STL、实物和版图照片都在仓库里 |
| **手机端马上能验证** | 微信小程序 Poyu Sport 连上设备就能看运动模式、概率、电量、气压和爬升 |
| **断电不丢状态** | 爬升高度、圈数、海平面气压、文件计数、启动次数和时间在断电后保留 |
| **原始数据留在本地** | 数据写存储卡、训练在本机，不要求上传云端 |

## 硬件

- ESP32-S3-WROOM-1 模组、USB-C、microSD 卡座、IMU、气压计、电量计
- Altium 工程、实物照片、PCB 版图与外壳（正面/反面 STL）见 [hardware/](hardware/)

<table>
<tr>
<td align="center"><img src="hardware/images/board-front.jpg" width="360" alt="POYU Smart Wear 实物正面"><br>实物正面</td>
<td align="center"><img src="hardware/images/board-back.jpg" width="360" alt="POYU Smart Wear 实物背面"><br>实物背面</td>
</tr>
<tr>
<td align="center"><img src="hardware/images/pcb-front.jpg" width="360" alt="PCB 顶层"><br>PCB 顶层（元件面）</td>
<td align="center"><img src="hardware/images/pcb-back.png" width="360" alt="PCB 底层"><br>PCB 底层（SD 卡座面）</td>
</tr>
</table>

### 外壳

<table>
<tr>
<td align="center"><img src="hardware/enclosure/images/shell-assembled.png" width="230" alt="外壳装配图"><br>装配图（渲染）</td>
<td align="center"><img src="hardware/enclosure/images/shell-outside.jpg" width="230" alt="外壳外部"><br>外壳外部</td>
<td align="center"><img src="hardware/enclosure/images/shell-inside.jpg" width="230" alt="外壳内部"><br>外壳内部（四角固定立柱）</td>
</tr>
</table>

## 测试前端（微信小程序）

<img src="docs/images/poyu-sport.jpg" width="360" alt="Poyu Sport 测试界面">

配套测试前端是微信小程序 **Poyu Sport**：连接广播名 `POYUSPORT` 的设备，Service / Characteristic UUID 见 [docs/FRONTEND.md](docs/FRONTEND.md)。小程序闭源、不在本仓库；没有小程序时可用 nRF Connect / LightBlue 按同一 UUID 测试。

## 目录结构

```text
smart_wear_open/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults          # 从原工程导出的构建配置
├── main/
│   ├── main.cpp / Initialize.cpp
│   ├── include/                # 传感器、算法、AI、任务头文件
│   ├── src/                    # C++ 实现
│   ├── ulp/                    # ULP RISC-V 程序（默认未启用）
│   ├── tusb/                   # TinyUSB 描述符
│   ├── scripts/ulp_build.py
│   └── idf_component.yml
├── components/                 # 第三方库，保留各自 LICENSE
├── training/                   # 训练 / 量化 / 评估脚本
│   └── tools/                  # 数据检查、拆分、滤波、模型查看
├── models/model_12feat.tflite  # 参考模型
├── docs/
│   ├── protocol.md             # BLE TLV 协议
│   ├── FRONTEND.md             # 微信小程序测试说明
│   ├── TRAINING.md             # 采集、标注、训练、量化与部署全流程
│   ├── ENVIRONMENT.md          # 开发环境、工具链与版本
│   ├── BUILD.md                # 编译与烧录
│   └── DATASET.md              # 数据格式与标注
└── hardware/                   # Altium 工程 + 库，CERN-OHL-W-2.0
```

## 快速开始

环境：ESP-IDF **5.3.1**（或兼容的 5.3.x），Python 3.10+。完整版本矩阵见 [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md)。

```bash
git clone <this-repo> smart_wear_open
cd smart_wear_open
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

首次构建会通过 `main/idf_component.yml` 拉取 `arduino-esp32`、`esp-sr`、`esp-dsp` 等托管组件，需要网络。`managed_components/` 和 `build/` 已被忽略。

详细步骤见 [docs/BUILD.md](docs/BUILD.md)。

## 模型与推理

- 输入：`[1, 75, 12]`，float32（int8 变体为对应量化输入）
- 输出：`[1, 4]`，softmax：`0 其他 / 1 步行 / 2 骑行 / 3 跑步`
- 特征：`roll, pitch, accel_x/y/z, gyro_x/y/z, vert_line_accel, accel_mag, hor_accel, vert_percent`（12 维；`TF_LITE_USE_PARAMETER_13=1` 时为 13 维含 yaw）
- 窗口：75 帧，步长 40；同一窗口内标签必须一致

`main/SW_define.h` 里两个开关：

```c
#define ENABLE_DEDUCTION_FLAG       1   // 1: 推理；0: 采集
#define TFLITE_INT8_NORMALIZE_FLAG  0   // 1: int8 模型；0: float32 模型
```

切换 int8 时，需要用 `training/` 里的量化脚本重新生成 `model_io_int8.h`。

## 训练与量化

完整流程见 [docs/TRAINING.md](docs/TRAINING.md)：设备端采集、数据检查、标注、窗口化、训练、剪枝/量化、固件加载与端侧验证。

```powershell
cd training
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt

$env:TRAIN_DATA = "<train_data_dir>"
python Quantization_Conv1D_LSTM.py

$env:EVAL_DATA = "<test_data_dir>"
$env:TFLITE_MODEL = ".\models\model_12feat.tflite"
python eval_tflite.py
```

数据格式、目录命名和标签规则见 [docs/DATASET.md](docs/DATASET.md) 与 [training/README.md](training/README.md)。数据检查、拆分、滤波和模型查看工具在 [training/tools/](training/tools/)。

## BLE 协议

`docs/protocol.md` 定义了帧格式（`0xAA 0x55` 起始、TLV、CRC16-CCITT-FALSE）和 `STATE` / `TELEMETRY` / `CONTROL` 等消息。枚举里预留了 6 类运动，当前模型只输出 4 类。

## 已知限制

- 这是**研究/学习用途**的参考实现，不是医疗设备，也不构成健康建议。
- 分类准确率取决于采集数据、佩戴位置和设备姿态；仓库不包含原始数据集，无法直接复现论文级指标。
- 当前只支持 4 类运动，不识别羽毛球动作、跌倒或具体活动类型。
- SoftAP 默认密码是演示值（`11111111`），WebSocket 没有鉴权，只应在可信网络中使用；正式产品请改为配网 + 鉴权。
- 部分历史注释/命名保留了原工程的写法，欢迎 PR 清理。
- 硬件工程只含 Altium 源文件，不含 Gerber / BOM / 坐标文件；3D 模型链接已脱敏，打开时可能提示缺失，重新链接即可。
- 硬件版本为 `版本18_four`：原理图 2026-01-07、PCB 2026-01-06；打样前请在 Altium 中重新编译并跑 DRC。
- 没有实现 OTA 推送；`partitions.csv` 只是保留了双 app 槽位。
- OLED 驱动头文件存在，但未参与默认构建；当前固件不驱动屏幕。
- ULP 源码保留在 `main/ulp/`，但默认未启用；低功耗唤醒使用 GPIO ext0/ext1。
- TinyUSB MSC 初始化调用被注释，默认不作为 U 盘暴露 SD 卡。

## 许可证

- 本项目原创固件与训练代码：**Apache License 2.0**，见 [LICENSE](LICENSE)。
- `components/` 与构建时拉取的组件：各自许可证（MIT / BSD / Apache-2.0 / MPL-2.0 / LGPL-2.1 / LGPL-3.0），**整个仓库不是单一的 Apache-2.0**。详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
- AsyncDelay、SoftWire、ESPAsyncWebServer、AsyncTCP 和 Arduino-ESP32 为 LGPL：只发布源码时保留声明即可；发布固件二进制或售卖设备时，还需满足 LGPL 的源码提供与可重新链接义务（AsyncTCP 是 LGPL-3.0，另有安装信息要求）。
- 模型权重：随本项目以 Apache-2.0 提供；训练数据不在仓库内。
- 硬件设计：`hardware/` 下的 Altium 工程，**CERN-OHL-W-2.0**，见 [hardware/LICENSE](hardware/LICENSE)。
- “POYU / 泊宇”名称与标识不随代码授权。

## 贡献

欢迎 issue / PR：驱动适配、量化精度、更多运动类别、文档与注释清理。提交前请确保不包含个人数据、密钥或厂商版权资料。

## 联系

- 邮箱：[18872264430@163.com](mailto:18872264430@163.com)
- 也可以直接在仓库提 issue。
