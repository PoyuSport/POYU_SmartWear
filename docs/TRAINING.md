# 边缘计算训练与部署流程

本文按“设备采集 → 数据检查 → 标注 → 窗口化 → 训练 → 剪枝/量化 → TFLite → 固件加载 → 端侧推理”的顺序，说明这个工程实际使用的方法。它描述的是**当前代码的行为**，不是通用的人体活动识别标准。

```text
ESP32-S3 + IMU
   │ 50 Hz，CSV 写 SD 卡
   ▼
CSV（roll/pitch/.../type/time）
   │ 检查列、拆分、滤波、补 Sample_Block
   ▼
按目录/列标注（0 其他 / 1 步行 / 2 骑行 / 3 跑步）
   │ 75 帧窗口，步长 40，同块同标签
   ▼
Conv1D + LSTM + 统计特征 → Dense 4 类
   │ 剪枝、量化、TFLite 转换
   ▼
model_12feat.tflite → model_io12_float32.h
   │ 固件 include，TFLite Micro 加载
   ▼
端侧推理，BLE 上报运动类型/概率
```

## 1. 设备端数据采集

### 1.1 固件配置

采集时把 `main/SW_define.h` 改成：

```c
#define ENABLE_DEDUCTION_FLAG       0   // 0: 只采集，不做推理
#define TFLITE_INT8_NORMALIZE_FLAG  0   // 采集阶段用 float32 通道
```

`ENABLE_DEDUCTION_FLAG=1` 时才会初始化 TFLite、分配 tensor arena 并执行推理；采集阶段设为 0 可以减少功耗和干扰。CSV 记录本身不依赖这个开关。

### 1.2 采样与文件

- 当前固件按 `SAMPLE_RATE_HZ`（默认 **50 Hz**）处理数据，模型窗口也是按 50 Hz 设计的。
- 数据写入 SD 卡，文件头固定为：

```text
roll,pitch,yaw,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,
vert_line_accel,accel_mag,hor_accel,vert_percent,
quat_x,quat_y,quat_z,quat_w,rpm,type,time
```

- `type` 是固件当时的运动类型输出（采集模式下通常为 0），**不是人工标注**；训练不要拿它当标签。
- `time` 是设备本地时间字符串，用于排查时间对齐，不参与 12 维特征。
- 文件按 `MMDDHHMM.csv` 命名，超过大小会自动轮转；一个文件应尽量只包含**一种活动**。
- CSV 里没有 `Sample_Block` 和 `Train_Label`，这两个字段由训练侧补充或从目录名推断。

### 1.3 采集协议建议

为了让训练和端侧表现一致，采集时固定以下条件：

1. 一次只录一种活动（走、跑、骑、静止/其他），每种至少 3–5 段、每段 30–60 秒；
2. 覆盖不同人、不同佩戴位置（左/右裤兜）、不同速度；
3. 活动之间留 2–3 秒静止，方便切分；
4. 不要在同一个 CSV 里混走、跑、骑——训练脚本会把整个文件当成一个块和一个标签；
5. 记录设备型号、固件版本、采样率、佩戴方式；
6. 保留原始 CSV，不要只留截图或聚合结果。

## 2. 数据检查与清洗

把 SD 卡里的 CSV 按活动分目录放，例如：

```text
train_data/
├── WALK_20260101/
├── RUN_20260102/
├── BYCLE_20260103/
└── OTHER_20260104/
```

先跑检查：

```powershell
python training/tools/dataset_stats.py train_data --json stats.json
```

它会报告文件数、总行数、标签分布、缺列、空文件和读取失败。重点看：

- 12 个必需列是否齐全（表头被截断时脚本会按前 8 个字符匹配）；
- 每个文件是否只有一种活动；
- 标签是否严重失衡；
- 是否有 0 行或大量缺测的文件。

需要拆分大文件时：

```powershell
python training/tools/split_csv.py train_data/WALK_xxx/big.csv --rows 50000
```

需要平滑/去趋势时：

```powershell
python training/tools/bandpass_filter.py data.csv --column accel_mag --fs 50 --low 0.1 --high 20
```

注意：滤波是**可选**的。训练脚本默认使用原始 12 维特征；如果训练时滤波、固件里不滤波，端侧输入分布会不一致。要么两边都做，要么都不做。

如果 CSV 没有 `Sample_Block`，`training/process_blocks.py` 可以按连续块补编号、去重和重排；当前训练脚本在缺少该列时会把**整个文件当成一个块**。

## 3. 标注

### 3.1 标签来源

训练脚本按以下优先级取标签：

1. CSV 里有 `Train_Label` 列：直接用该列；
2. 否则看文件路径（不区分大小写）：
   - 包含 `WALK` → 1
   - 包含 `BYCLE` 或 `CYCLE` → 2
   - 包含 `RUN` → 3
   - 其他 → 0

| 类别 | 含义 | 固件 `MotionType` |
|---:|---|---|
| 0 | 其他 / 未知 | 0 UNKNOWN |
| 1 | 步行 | 1 WALK |
| 2 | 骑行 | 2 CYCLE |
| 3 | 跑步 | 3 RUN |

固件里还有 STILL=4、CLIMB=5、OTHER=6 的枚举，但当前 4 类模型不会输出它们。

### 3.2 标注规则

- 一个窗口（75 帧）内标签必须唯一；同一 `Sample_Block` 内也建议只有一种活动。
- 不要把“走 + 跑”放在同一个文件里再靠列标签区分；窗口切分要求同一块同标签，混在一起会被丢弃或误标。
- 过渡段（起步、停下、换动作）可以标为“其他”，不要硬塞进三类运动。
- 标注是人工决定，不根据起跳/相持候选反推。

### 3.3 划分数据集

正确做法是**按采集段划分**，不是按窗口随机划分。同一段连续采集的窗口高度相关，随机划分会让测试集泄漏，指标虚高。

当前 `training/Quantization_Conv1D_LSTM.py` 已改为：

```python
splitter = GroupShuffleSplit(n_splits=1, test_size=0.2, random_state=42)
train_idx, test_idx = next(splitter.split(X, y, groups))
```

`groups` 来自 `Sample_Block`（没有该列时一个文件一个组）。这样同一段不会同时出现在训练和测试里。

## 4. 特征与窗口

### 4.1 12 维特征

| 特征 | 含义 | 单位 |
|---|---|---|
| `roll` | 横滚角 | 度 |
| `pitch` | 俯仰角 | 度 |
| `accel_x/y/z` | 东北天坐标系加速度 | g |
| `gyro_x/y/z` | 角速度 | °/s |
| `vert_line_accel` | 竖直方向线性加速度 | g |
| `accel_mag` | 三轴加速度模长 | g |
| `hor_accel` | 水平方向加速度强度 | g |
| `vert_percent` | 垂直占比 | 比例 |

训练默认 **12 维，不含 yaw**。如果启用 `TF_LITE_USE_PARAMETER_13=1`，固件会按 13 维（增加 yaw）填充，训练侧也必须用 13 列重新训练，不能混用。

### 4.2 窗口

```python
create_time_windows(feature_data, labels, sample_ids,
                    window_size=75, step=40)
```

- 窗口 75 帧：50 Hz 下约 **1.5 秒**；
- 步长 40 帧：约 **0.8 秒**；
- 窗口必须落在同一个 `Sample_Block` 内，且 75 个标签一致；
- 不满足条件的窗口会直接丢弃。

如果改变采样率或窗口长度，必须同时改固件里的缓冲/模型输入，并重新训练；不能只改训练脚本。

## 5. 模型结构

`build_prunable_hybrid_model()` 当前结构：

```text
输入 (75, 12)
├── LSTM(32, tanh/sigmoid, unroll=True) → GlobalAveragePooling1D
├── Conv1D(16, kernel=5, relu) → MaxPooling1D(2) → Flatten
└── GlobalAveragePooling1D + 逐特征标准差 → Concatenate
        ↓
Concatenate 三个分支 → Dense(64, relu, L2) → Dropout(0.3) → Dense(4, softmax)
```

输出 4 类：其他 / 步行 / 骑行 / 跑步。

## 6. 训练

### 6.1 命令

```powershell
cd training
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt

$env:TRAIN_DATA = "D:\path\to\train_data"
python Quantization_Conv1D_LSTM.py
```

### 6.2 当前超参数

| 项目 | 值 |
|---|---|
| 输入 | `(75, 12)` |
| 优化器 | Adam，学习率 1e-3 |
| 损失 | `sparse_categorical_crossentropy` |
| batch size | 32 |
| epochs | 30 |
| EarlyStopping | patience=8，恢复最佳权重 |
| 剪枝 | 5% → 10%，begin_step=2000，end_step=5000，排除 LSTM |
| 验证集 | 20%，按 `Sample_Block` 分组 |
| 随机种子 | 42（TensorFlow 不保证完全可复现） |

### 6.3 训练输出

- `trained_model_12feat.h5`
- `model_12feat.tflite`（float32 输入、int8 算子）
- `model_io12_float32.h`（C 数组，`model_data`，8 字节对齐）

训练脚本会打印数据形状、每类样本数、训练/验证准确率，并调用 `plot_static_comparison()` 画预测对比。

## 7. 量化与导出

当前生效的转换分支：

```python
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset
converter.target_spec.supported_ops = [
    tf.lite.OpsSet.TFLITE_BUILTINS,
    tf.lite.OpsSet.TFLITE_BUILTINS_INT8,
]
converter.inference_input_type = tf.float32
converter.inference_output_type = tf.float32
```

- `representative_dataset()` 从训练集随机抽 500 个窗口，用于 int8 校准；
- 模型输入/输出仍是 float32，内部算子 int8，所以固件走 `TFLITE_INT8_NORMALIZE_FLAG=0` 的 float32 路径；
- 当前 `model_12feat.tflite` 约 **629 KB**；
- 生成的 `model_io12_float32.h` 里数组名是 `model_data`，和 `Initialize.cpp` 的 `tflite::GetModel(model_data)` 对应。

要生成真正的 int8 输入模型，需要：

1. 取消 `converter.inference_input_type/output_type = tf.int8` 的注释；
2. 取消训练侧 `normalize_features()` 的注释，让输入落在 [-1, 1]；
3. 固件设 `TFLITE_INT8_NORMALIZE_FLAG=1`，并按同样规则归一化 + 量化；
4. 重新验证输入 scale/zero point 与固件一致。

只改其中一边会导致输入分布不匹配，端侧输出会明显变差。

## 8. 固件加载模型

### 8.1 开关

`main/SW_define.h`：

```c
#define ENABLE_DEDUCTION_FLAG       1   // 1: 推理；0: 只采集
#define TFLITE_INT8_NORMALIZE_FLAG  0   // 0: float32；1: int8 + 手工归一化
#define TF_LITE_USE_PARAMETER_13    0   // 0: 12 维；1: 13 维（含 yaw）
```

`Initialize.cpp` 按这两个开关选择 `model_io_int8.h` 或 `model_io12_float32.h`。

### 8.2 初始化

`init_AI()` 做了这些事：

1. `tflite::GetModel(model_data)` 取得模型；
2. 从 PSRAM 分配 **256 KB** tensor arena，16 字节对齐；
3. `MicroMutableOpResolver<27>` 注册 Add/Mul/Conv2D/Softmax/FullyConnected/Quantize/Dequantize 等 27 个算子；
4. `AllocateTensors()`，失败会打印 `Tensor allocation failed`；
5. 保存 `input`/`output` 指针；int8 时读取 `input_scale`、`input_zero_point`、`output_scale`、`output_zero_point`；
6. 打印 `arena_used_bytes()`，用于判断 256 KB 是否够用。

模型缺算子或 arena 不够，通常会在 `AllocateTensors()` 或第一次 `Invoke()` 报错。

### 8.3 端侧推理

`TaskManager.cpp` 的 AI 任务：

- 每 50 帧（`INFERENCE_DATA_NUM`）把特征写入 PSRAM 双缓冲（`AI_BUFFER_SIZE=100` 帧）；
- 推理时把 `input->bytes` 从 PSRAM 缓冲拷入输入 tensor（当前模型需要 75×12）；
- `interpreter->Invoke()` 后取 4 类输出；
- 取最大概率；**概率 < 0.4 时强制归为类别 0（未知）**；
- 通过 BLE 上报 `MotionType` 与概率（0 未知、1 步行、2 骑行、3 跑步）。

如果换窗口长度或特征维度，必须同步修改 `DATA_SHAPE`、`INFERENCE_DATA_NUM`、`AI_BUFFER_SIZE` 和模型输入，不能只换 `.h`。

## 9. 端侧验证

1. 烧录固件，串口确认 `Tensor allocation success`、`arena_used_bytes`；
2. 打开微信小程序 **Poyu Sport**，连接 `POYUSPORT`，确认 Notify；
3. 走动/跑步/骑行，观察「运动模式/概率」是否变化；
4. 串口查看 `类别:%d,概率:%f` 日志；
5. 记录推理耗时、内存占用、功耗和误判场景；
6. 用同一批数据在 PC 上跑 `eval_tflite.py`，和端侧结果对照。

`eval_tflite.py` 会重新加载测试集、还原同一划分，输出准确率/混淆矩阵/逐样本对比。

## 10. 常见问题

| 现象 | 可能原因 |
|---|---|
| 训练报缺列 | CSV 表头被截断或不是 12 列；用 `dataset_stats.py` 检查 |
| 准确率异常高 | 按窗口随机划分导致同段泄漏；确认使用 `GroupShuffleSplit` |
| 端侧一直输出未知 | 概率 < 0.4；检查特征顺序、归一化、窗口长度是否与训练一致 |
| AllocateTensors 失败 | 算子缺失或 tensor arena 太小；看串口错误，增大 256 KB |
| 模型加载后输出乱码 | `.h` 数组不是 `model_data`，或 int8/float32 开关不匹配 |
| 训练/端侧结果差异大 | 训练用了滤波/归一化，固件没有；或采样率不同 |
| 小概率类别总被忽略 | 类别不平衡；先补数据，再考虑 class weight |

## 11. 边界

- 当前只有 4 类，不识别羽毛球动作、跌倒、上下楼等。
- 采集数据没有质量列，饱和、断连、佩戴松动需要人工剔除。
- 训练集不含视频真值，准确率只代表这批标注数据上的分类一致性。
- 50 Hz + 75 帧窗口是当前固定口径；换硬件、换采样率、换佩戴位置都要重新采集和训练。
