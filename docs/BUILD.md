# 编译与烧录

## 环境

| 项目 | 版本/说明 |
|---|---|
| ESP-IDF | 5.3.1（5.3.x 理论兼容，未逐一验证） |
| 目标芯片 | ESP32-S3，Octal PSRAM 80 MHz |
| Flash | 16 MB，QIO，40 MHz |
| Python | 3.10+（仅训练脚本需要 TensorFlow） |
| 串口驱动 | CH343 / CP210x，按板子实际 |

Windows 可用 Espressif IDE / ESP-IDF Installer；Linux/macOS 用 git 安装脚本。安装后确认：

```bash
idf.py --version
```

## 构建

```bash
cd smart_wear_open
idf.py set-target esp32s3
idf.py build
```

首次构建会联网拉取 `managed_components`：

- `espressif/arduino-esp32` 3.3.1
- `espressif/esp-sr` >= 1.5.0
- `espressif/esp_tinyusb` 2.0.1
- `espressif/esp-dsp` ^1.7.0

仓库里的 `components/` 是随源码分发的本地组件，不经过组件管理器。

## 配置

`sdkconfig.defaults` 已包含关键配置：

```text
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

如果要重新生成配置：

```bash
idf.py menuconfig
```

重点检查：

- `Component config → ESP PSRAM`：Octal / 80 MHz
- `Serial flasher config`：16 MB
- `Partition Table`：Custom，`partitions.csv`

## 烧录

```bash
idf.py -p COM5 flash monitor
```

Linux 下把 `COM5` 换成 `/dev/ttyUSB0` 或 `/dev/ttyACM0`。退出 monitor：`Ctrl+]`。

烧录后建议先用微信小程序 **Poyu Sport** 做一次连接测试，步骤和 BLE 参数见 [FRONTEND.md](FRONTEND.md)。

## 模型开关

`main/SW_define.h`：

```c
#define ENABLE_DEDUCTION_FLAG       1   // 1 推理，0 采集
#define TFLITE_INT8_NORMALIZE_FLAG  0   // 0 float32，1 int8
#define TF_LITE_USE_PARAMETER_13    0   // 0 十二维，1 十三维（含 yaw）
```

切换后需要同步更换 `main/include/AI/` 下对应的模型头文件，并重新编译。

## ULP

`main/ulp/ulp_main.c` 编译为 RISC-V ULP 程序，由 `ulp_embed_binary()` 嵌入主固件；`main/scripts/ulp_build.py` 是辅助构建脚本。ULP 负责低功耗加速度阈值唤醒，不参与 AI 推理。

## 常见问题

**组件下载失败**：检查网络/代理；可先用 `idf.py reconfigure` 单独拉取组件。

**PSRAM 初始化失败**：确认板子是 Octal PSRAM，且 `CONFIG_SPIRAM_MODE_OCT=y`；QSPI PSRAM 的板子需要改配置。

**模型输入维度不匹配**：`DATA_SHAPE`、模型头文件和 `model_*.tflite` 必须同为 12 或 13 维。

**启动后反复重启**：先看 monitor 里的 panic 信息；常见是分区表与 16 MB Flash 不匹配，或 PSRAM 配置错误。

**想先验证硬件**：把 `ENABLE_DEDUCTION_FLAG` 设为 0，只跑采集与通信，不加载模型。
