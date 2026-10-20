# 训练与量化

这里是从原始 CSV 到 TFLite 模型的参考管线。脚本保留了原工程的实验痕迹（部分代码被注释），当前生效的是文件末尾的实现。

## 文件

| 文件 | 作用 |
|---|---|
| `Quantization_Conv1D_LSTM.py` | 加载 CSV、切窗口、训练 Conv1D+LSTM 混合模型、剪枝、量化、导出 `.tflite` |
| `eval_tflite.py` | 重新加载测试集，评估 TFLite 模型，输出准确率/混淆矩阵/逐样本对比 |
| `process_blocks.py` | 给 CSV 的 `Sample_Block` 重新编号，整理连续块 |
| `tools/dataset_stats.py` | 扫描 CSV 的必需列、行数和标签分布，先筛数据 |
| `tools/split_csv.py` | 把大 CSV 按固定行数流式拆分，降低单文件读取压力 |
| `tools/bandpass_filter.py` | 对 CSV 单列做零相位 Butterworth 带通滤波 |
| `tools/inspect_tflite.py` | 查看 TFLite 的输入/输出 shape、dtype 和量化参数 |
| `QCL.md` | 原工程笔记 |

## 辅助脚本

```powershell
# 先看数据有没有缺列、空文件、标签分布
python tools/dataset_stats.py ..\train_data --json stats.json

# 单个 CSV 太大时按 5 万行拆分
python tools/split_csv.py ..\train_data\big.csv --rows 50000

# 对某一列做 0.1–20 Hz 带通（需要 scipy）
python tools/bandpass_filter.py data.csv --column accel_mag --fs 104 --low 0.1 --high 20

# 查看模型输入/输出和量化参数（需要 tensorflow）
python tools/inspect_tflite.py ..\models\model_12feat.tflite --ops
```

`dataset_stats.py` 只读表头和逐行计数，不会把整个数据集载入内存；`split_csv.py` 也是流式读写。

## 环境

```powershell
cd training
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

建议 Python 3.10 / 3.11；TensorFlow 2.15+ 与 `tensorflow-model-optimization` 版本要匹配。

## 训练

数据目录格式见 [../docs/DATASET.md](../docs/DATASET.md)。

```powershell
$env:TRAIN_DATA = "<train_data_dir>"
python Quantization_Conv1D_LSTM.py
```

脚本会在当前目录生成：

- `trained_model_12feat.h5`
- `model_12feat.tflite`（float32 或量化版本，取决于脚本里的转换分支）

当前脚本默认保存 float32 TFLite；要生成 int8 版本，取消文件中 int8 转换段落的注释，并提供 `representative_dataset`。

## 评估

```powershell
$env:EVAL_DATA = "<test_data_dir>"
$env:TFLITE_MODEL = ".\models\model_12feat.tflite"
python eval_tflite.py
```

## 生成固件用的 C 头文件

TFLite 转 C 数组可以用 `xxd`：

```bash
xxd -i model_12feat.tflite > model_io12_float32.h
```

然后手工把数组名改成 `model_data`，并保留 8 字节对齐：

```c
const unsigned char model_data[] __attribute__((aligned(8))) = { ... };
```

把文件放到 `main/include/AI/`，并让 `Initialize.cpp` 的 include 指向它。int8 模型同理，生成 `model_io_int8.h`。

## 注意

- 训练/测试集要按采集段划分，不要按窗口随机划分。
- 仓库不包含原始数据，所以这里的脚本不能直接复现某个准确率数字。
- 剪枝和量化会改变模型体积与精度，切换后要在真机上重新验证。
