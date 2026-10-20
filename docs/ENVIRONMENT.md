# 开发与验证环境

本文记录当前工程实际使用的开发环境。换机器、换芯片或升级工具链时，建议先对照这一页。

## 1. 硬件

| 项目 | 配置 |
|---|---|
| 主控 | ESP32-S3-WROOM-1（模组具体型号以丝印/BOM 为准） |
| Flash | 16 MB（`CONFIG_ESPTOOLPY_FLASHSIZE="16MB"`） |
| PSRAM | Octal PSRAM，80 MHz（`CONFIG_SPIRAM_MODE_OCT` / `CONFIG_SPIRAM_SPEED_80M`） |
| IMU | ICM-20948 / MPU-9250 兼容驱动；SC7A22H 驱动 |
| 气压计 | MS5611 |
| 电量计 | MAX17043 |
| 外设 | microSD、USB-C、OLED、EEPROM、RGB LED |
| 调试 | USB-C 串口（CH343 / CP210x 驱动） |
| 硬件版本 | `版本18_four`：原理图 2026-01-07，PCB 2026-01-06 |

## 2. 固件开发

| 项目 | 版本 / 说明 |
|---|---|
| 操作系统 | **Windows 11 x64**（当前开发与验证环境） |
| ESP-IDF | **5.3.1** |
| 目标芯片 | `esp32s3` |
| Arduino-ESP32 | 3.3.1（由组件管理器拉取） |
| esp-sr | `>=1.5.0` |
| esp_tinyusb | 2.0.1 |
| esp-dsp | `^1.7.0` |
| 编辑器 | VS Code + Espressif IDF 扩展，或直接用 `idf.py` |
| 构建用 Python | 随 ESP-IDF 安装的 Python 3.10+ |
| Git | 任意较新版本 |
| 串口驱动 | CH343 / CP210x |

验证命令：

```powershell
idf.py --version
# ESP-IDF v5.3.1
```

## 3. 训练与量化

| 项目 | 版本 / 说明 |
|---|---|
| 操作系统 | Windows 11 x64（当前验证；Linux/macOS 未验证） |
| Python | **3.10 / 3.11 推荐**（TensorFlow 2.15–2.19 兼容性最好） |
| TensorFlow | `>=2.15,<2.20` |
| tensorflow-model-optimization | `>=0.8` |
| numpy / pandas / scikit-learn / matplotlib / scipy | 见 `training/requirements.txt` |
| 计算设备 | CPU 即可；有 NVIDIA GPU 可装 CUDA 版 TensorFlow，非必需 |
| 内存 / 磁盘 | 建议 16 GB 内存、10 GB 以上空闲磁盘 |

安装：

```powershell
cd training
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

## 4. 硬件设计

| 项目 | 版本 / 说明 |
|---|---|
| EDA | **Altium Designer 19 或更新版本**（工程路径来自 AD19） |
| 外壳 | 只提供 STL；打印用任意切片软件即可 |
| 结构源文件 | 仓库不再包含 SolidWorks 文件；如需编辑原始 SLDPRT，需要 SolidWorks |

## 5. 测试前端

| 项目 | 说明 |
|---|---|
| 手机 | 微信 + 蓝牙 4.0 以上 |
| 小程序 | **Poyu Sport**（闭源，不在仓库内） |
| 替代工具 | nRF Connect / LightBlue，按 `docs/FRONTEND.md` 的 UUID 连接 |
| 串口 | `idf.py -p COMx monitor`，用于看推理日志和 tensor arena |

## 6. 兼容性说明

- 当前只在 **Windows 11 + ESP-IDF 5.3.1 + ESP32-S3** 上验证。
- ESP-IDF 5.3.x 其他小版本理论兼容，未逐一验证。
- Python 3.12 与 TensorFlow 2.15 的组合没有验证；建议用 3.10/3.11。
- 更换模组、Flash 或 PSRAM 类型时，需要同步修改 `sdkconfig.defaults`。
- 更换采样率、窗口长度或特征维度时，训练、模型头文件和固件三边都要改，详见 [TRAINING.md](TRAINING.md)。

## 7. 未验证 / 不承诺

- Linux、macOS 下的构建与训练；
- 其他 ESP32-S3 模组、其他 PSRAM 容量；
- 其他 TensorFlow / ESP-IDF 版本组合；
- 量产环境下的 EMC、功耗、温升和长期稳定性。
