# # -*- coding: utf-8 -*-
# import tensorflow as tf
# import numpy as np
# import pandas as pd
# import glob
# import matplotlib.pyplot as plt
# from sklearn.model_selection import train_test_split
# import os

# # ================= 配置区域 =================
# # 数据集文件夹路径 (与训练脚本保持一致)
# DATA_PATH = './test_data/'
# # 要评估的 TFLite 模型路径
# TFLITE_MODEL_PATH = 'model_13feat.tflite'
# # ===========================================

# # 类别定义 (必须与训练一致)
# CLASS_MAP = {
#     0: "其他",
#     1: "步行",
#     2: "骑行",
#     3: "跑步"
# }

# # 特征列定义
# FEATURE_COLUMNS = [
#     'roll', 'pitch', 'yaw',                
#     'accel_x', 'accel_y', 'accel_z',       
#     'gyro_x', 'gyro_y', 'gyro_z',          
#     'vert_line_accel', 'accel_mag',        
#     'hor_accel', 'vert_percent'            
# ]

# def normalize_columns(csv_columns):
#     mapping = {}
#     missing = []
#     clean_csv_cols = {col.strip(): col for col in csv_columns}
#     for req in FEATURE_COLUMNS:
#         if req in clean_csv_cols:
#             mapping[req] = clean_csv_cols[req]
#             continue
#         match = next((actual for clean, actual in clean_csv_cols.items() 
#                       if clean.startswith(req[:8])), None)
#         if match:
#             mapping[req] = match
#         else:
#             missing.append(req)
#     return mapping, missing

# def create_time_windows(data, labels, sample_ids, window_size=75, step=40):
#     windows = []
#     window_labels = []
#     # 只要数据，不需要 group
#     for start in range(0, len(data) - window_size + 1, step):
#         end = start + window_size
#         window = data[start:end]
#         current_sample_id = sample_ids[start]
#         if np.all(sample_ids[start:end] == current_sample_id):
#             windows.append(window)
#             current_labels = labels[start:end]
#             if len(np.unique(current_labels)) == 1:
#                 window_labels.append(current_labels[0])
#     return np.array(windows, dtype=np.float32), np.array(window_labels, dtype=np.int32)

# def load_data_and_recreate_test_set():
#     """
#     完全复刻训练代码的数据加载逻辑，利用 random_state=42 还原 X_test
#     """
#     print("⏳ 正在重新加载数据以还原对照组(Test Set)...")
#     file_list = glob.glob(os.path.join(DATA_PATH, '**', '*.[cC][sS][vV]'), recursive=True)
#     if not file_list:
#         raise FileNotFoundError(f"没有找到数据文件: {DATA_PATH}")

#     all_data = []
#     global_max_sample_id = -1

#     for file_path in file_list:
#         try:
#             header_df = pd.read_csv(file_path, nrows=0)
#             col_map, missing = normalize_columns(header_df.columns.tolist())
#             if missing: continue

#             cols_to_read = list(col_map.values())
#             # 简单的列名推断逻辑
#             csv_cols = header_df.columns.tolist()
#             label_col = next((c for c in csv_cols if 'Train_Label' in c or 'Train_Labe' in c), None)
#             block_col = next((c for c in csv_cols if 'Sample_Block' in c), None)

#             if label_col: cols_to_read.append(label_col)
#             if block_col: cols_to_read.append(block_col)

#             df = pd.read_csv(file_path, usecols=cols_to_read)
#             rename_dict = {v: k for k, v in col_map.items()}
#             if label_col: rename_dict[label_col] = 'Train_Label'
#             if block_col: rename_dict[block_col] = 'Sample_Block'
#             df.rename(columns=rename_dict, inplace=True)

#             for col in FEATURE_COLUMNS:
#                 df[col] = pd.to_numeric(df[col], errors='coerce')
#             df.dropna(subset=FEATURE_COLUMNS, inplace=True)
#             if len(df) == 0: continue

#             if 'Train_Label' not in df.columns:
#                 path_str = file_path.upper()
#                 if 'WALK' in path_str: val = 1
#                 elif 'BYCLE' in path_str or 'CYCLE' in path_str: val = 2
#                 elif 'RUN' in path_str: val = 3
#                 else: val = 0
#                 df['Train_Label'] = val

#             if 'Sample_Block' in df.columns:
#                 df['sample_id'] = (df['Sample_Block'] != df['Sample_Block'].shift()).cumsum() + global_max_sample_id
#             else:
#                 df['sample_id'] = global_max_sample_id + 1
#             global_max_sample_id = df['sample_id'].max()
#             all_data.append(df)
#         except:
#             continue

#     if not all_data: return None, None

#     combined_data = pd.concat(all_data, ignore_index=True)
#     feature_data = combined_data[FEATURE_COLUMNS].values.astype(np.float32)
#     sample_ids = combined_data['sample_id'].values.astype(np.int32)
#     labels = combined_data['Train_Label'].values.astype(np.int32)

#     X, y = create_time_windows(feature_data, labels, sample_ids, window_size=75, step=40)
    
#     # 关键步骤：使用相同的 random_state=42，以确保这里的 X_test 与训练时的 X_test 完全一致
#     _, X_test, _, y_test = train_test_split(
#         X, y, test_size=0.2, random_state=42, stratify=y
#     )
#     print(f"✅ 对照组数据还原完成。样本数: {len(X_test)}")
#     return X_test, y_test

# def run_tflite_inference(model_path, X_test):
#     """加载 .tflite 并运行推理"""
#     print(f"🤖 加载模型: {model_path}")
#     try:
#         interpreter = tf.lite.Interpreter(model_path=model_path)
#         interpreter.allocate_tensors()
#     except Exception as e:
#         print(f"载入模型失败: {e}")
#         return None

#     input_details = interpreter.get_input_details()
#     output_details = interpreter.get_output_details()
    
#     input_idx = input_details[0]['index']
#     output_idx = output_details[0]['index']
    
#     # 推理
#     preds = []
#     print("🚀 开始推理...")
#     for i, sample in enumerate(X_test):
#         # 增加 batch 维度: (75, 13) -> (1, 75, 13)
#         input_data = np.expand_dims(sample, axis=0)
        
#         interpreter.set_tensor(input_idx, input_data)
#         interpreter.invoke()
#         output_data = interpreter.get_tensor(output_idx)
        
#         # 记录预测的类别 (0, 1, 2, 3)
#         preds.append(np.argmax(output_data))
        
#         if i % 75 == 0:
#             print(f"\r进度: {i}/{len(X_test)}", end="")
#     print("\n推理完成!")
#     return np.array(preds)

# def plot_interactive_result(X_test, y_test, y_pred):
#     """交互式可视化函数"""
#     fig, ax = plt.subplots(figsize=(14, 7))
    
#     # 限制显示数量以保持界面流畅
#     display_limit = 500
#     indices = np.arange(min(len(y_test), display_limit))
    
#     y_true_disp = y_test[indices]
#     y_pred_disp = y_pred[indices]
#     X_disp = X_test[indices]

#     # 绘制
#     ax.plot(indices, y_true_disp, 'go-', alpha=0.5, label='True Label', picker=5)
#     ax.plot(indices, y_pred_disp, 'rx--', alpha=0.5, label='Model Prediction')
    
#     # 标记错误
#     errors = y_true_disp != y_pred_disp
#     ax.scatter(indices[errors], y_pred_disp[errors], color='purple', s=75, zorder=5, label='Error Point', picker=5)

#     ax.set_yticks(list(CLASS_MAP.keys()))
#     ax.set_yticklabels(list(CLASS_MAP.values()))
#     ax.set_title(f"TFLite Model Evaluation (Accuracy: {np.mean(y_test == y_pred):.2%})\nClick on points to view sensor data")
#     ax.legend()
#     ax.grid(True, axis='y', linestyle='--')

#     # 点击事件
#     def on_click(event):
#         if event.inaxes == ax and event.button == 1:
#             idx = int(round(event.xdata))
#             if 0 <= idx < len(indices):
#                 sample_data = X_disp[idx] # (75, 13)
                
#                 # 创建子弹窗显示波形
#                 sub_fig = plt.figure(figsize=(8, 4))
#                 sub_fig.canvas.manager.set_window_title(f"Sample #{idx} Waveform")
#                 sub_ax = sub_fig.add_subplot(111)
                
#                 # 绘制关键特征 (Roll/Pitch)
#                 try:
#                     roll_idx = FEATURE_COLUMNS.index('roll')
#                     pitch_idx = FEATURE_COLUMNS.index('pitch')
#                     acc_z_idx = FEATURE_COLUMNS.index('accel_z')
                    
#                     sub_ax.plot(sample_data[:, roll_idx], label='Roll', color='blue')
#                     sub_ax.plot(sample_data[:, pitch_idx], label='Pitch', color='orange')
#                     sub_ax.plot(sample_data[:, acc_z_idx], label='Acc Z', color='green', alpha=0.3)
#                 except:
#                     sub_ax.plot(sample_data[:, 0], label='Feat 0')
                
#                 status_text = "CORRECT" if y_true_disp[idx] == y_pred_disp[idx] else "ERROR"
#                 sub_ax.set_title(f"Idx: {idx} | True: {CLASS_MAP[y_true_disp[idx]]} | Pred: {CLASS_MAP[y_pred_disp[idx]]} [{status_text}]")
#                 sub_ax.legend()
#                 sub_fig.tight_layout()
#                 sub_fig.show()

#     fig.canvas.mpl_connect('button_press_event', on_click)
#     plt.show()

# if __name__ == "__main__":
#     if not os.path.exists(TFLITE_MODEL_PATH):
#         print(f"❌ 找不到模型文件: {TFLITE_MODEL_PATH}")
#         print("请先运行训练脚本生成 .tflite 文件")
#     else:
#         # 1. 还原数据
#         X_test, y_test = load_data_and_recreate_test_set()
        
#         if X_test is not None:
#             # 2. 运行 TFLite
#             y_pred = run_tflite_inference(TFLITE_MODEL_PATH, X_test)
            
#             # 3. 打印统计
#             if y_pred is not None:
#                 acc = np.mean(y_test == y_pred)
#                 print(f"\n📊 最终准确率: {acc:.2%}")
                
#                 # 4. 可视化
#                 print("正在打开可视化窗口...")
#                 plot_interactive_result(X_test, y_test, y_pred)


########################################################################################################################################


# -*- coding: utf-8 -*-
import tensorflow as tf
import numpy as np
import pandas as pd
import glob
import matplotlib.pyplot as plt
from sklearn.model_selection import train_test_split
import os

# ================= 配置区域 =================
# 数据集文件夹路径 (与训练脚本保持一致)
DATA_PATH = os.environ.get('EVAL_DATA', './test_data')
# 要评估的 TFLite 模型路径
TFLITE_MODEL_PATH = os.environ.get('TFLITE_MODEL', './models/model_12feat.tflite')
# ===========================================

# 类别定义 (必须与训练一致)
CLASS_MAP = {
    0: "其他",
    1: "步行",
    2: "骑行",
    3: "跑步"
}

# 特征列定义
FEATURE_COLUMNS = [
    'roll', 'pitch',              
    'accel_x', 'accel_y', 'accel_z',       
    'gyro_x', 'gyro_y', 'gyro_z',          
    'vert_line_accel', 'accel_mag',        
    'hor_accel', 'vert_percent'            
]

def normalize_columns(csv_columns):
    mapping = {}
    missing = []
    clean_csv_cols = {col.strip(): col for col in csv_columns}
    for req in FEATURE_COLUMNS:
        if req in clean_csv_cols:
            mapping[req] = clean_csv_cols[req]
            continue
        match = next((actual for clean, actual in clean_csv_cols.items() 
                      if clean.startswith(req[:8])), None)
        if match:
            mapping[req] = match
        else:
            missing.append(req)
    return mapping, missing

def create_time_windows(data, labels, sample_ids, window_size=75, step=40):
    windows = []
    window_labels = []
    # 只要数据，不需要 group
    for start in range(0, len(data) - window_size + 1, step):
        end = start + window_size
        window = data[start:end]
        current_sample_id = sample_ids[start]
        if np.all(sample_ids[start:end] == current_sample_id):
            windows.append(window)
            current_labels = labels[start:end]
            if len(np.unique(current_labels)) == 1:
                window_labels.append(current_labels[0])
    return np.array(windows, dtype=np.float32), np.array(window_labels, dtype=np.int32)

def load_data_and_recreate_test_set():
    """
    完全复刻训练代码的数据加载逻辑，利用 random_state=42 还原 X_test
    """
    print("⏳ 正在重新加载数据以还原对照组(Test Set)...")
    file_list = glob.glob(os.path.join(DATA_PATH, '**', '*.[cC][sS][vV]'), recursive=True)
    if not file_list:
        raise FileNotFoundError(f"没有找到数据文件: {DATA_PATH}")

    all_data = []
    global_max_sample_id = -1

    for file_path in file_list:
        try:
            header_df = pd.read_csv(file_path, nrows=0,low_memory=False)
            col_map, missing = normalize_columns(header_df.columns.tolist())
            if missing: continue

            cols_to_read = list(col_map.values())
            # 简单的列名推断逻辑
            csv_cols = header_df.columns.tolist()
            label_col = next((c for c in csv_cols if 'Train_Label' in c or 'Train_Labe' in c), None)
            block_col = next((c for c in csv_cols if 'Sample_Block' in c), None)

            if label_col: cols_to_read.append(label_col)
            if block_col: cols_to_read.append(block_col)

            df = pd.read_csv(file_path, usecols=cols_to_read,low_memory=False)
            rename_dict = {v: k for k, v in col_map.items()}
            if label_col: rename_dict[label_col] = 'Train_Label'
            if block_col: rename_dict[block_col] = 'Sample_Block'
            df.rename(columns=rename_dict, inplace=True)

            for col in FEATURE_COLUMNS:
                df[col] = pd.to_numeric(df[col], errors='coerce')
            df.dropna(subset=FEATURE_COLUMNS, inplace=True)
            if len(df) == 0: continue

            if 'Train_Label' not in df.columns:
                path_str = file_path.upper()
                if 'WALK' in path_str: val = 1
                elif 'BYCLE' in path_str or 'CYCLE' in path_str: val = 2
                elif 'RUN' in path_str: val = 3
                else: val = 0
                df['Train_Label'] = val

            if 'Sample_Block' in df.columns:
                df['sample_id'] = (df['Sample_Block'] != df['Sample_Block'].shift()).cumsum() + global_max_sample_id
            else:
                df['sample_id'] = global_max_sample_id + 1
            global_max_sample_id = df['sample_id'].max()
            all_data.append(df)
        except:
            continue

    if not all_data: return None, None

    combined_data = pd.concat(all_data, ignore_index=True)
    feature_data = combined_data[FEATURE_COLUMNS].values.astype(np.float32)
    sample_ids = combined_data['sample_id'].values.astype(np.int32)
    labels = combined_data['Train_Label'].values.astype(np.int32)

    X, y = create_time_windows(feature_data, labels, sample_ids, window_size=75, step=40)
    
    # 关键步骤：使用相同的 random_state=42，以确保这里的 X_test 与训练时的 X_test 完全一致
    _, X_test, _, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42, stratify=y
    )
    print(f"✅ 对照组数据还原完成。样本数: {len(X_test)}")
    return X_test, y_test

def run_tflite_inference(model_path, X_test):
    """加载 .tflite 并运行推理"""
    print(f"🤖 加载模型: {model_path}")
    try:
        interpreter = tf.lite.Interpreter(model_path=model_path)
        interpreter.allocate_tensors()
    except Exception as e:
        print(f"载入模型失败: {e}")
        return None

    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    
    input_idx = input_details[0]['index']
    output_idx = output_details[0]['index']
    
    # 推理
    preds = []
    print("🚀 开始推理...")
    for i, sample in enumerate(X_test):
        # 增加 batch 维度: (75, 2) -> (1, 75, 12)
        input_data = np.expand_dims(sample, axis=0)
        
        interpreter.set_tensor(input_idx, input_data)
        interpreter.invoke()
        output_data = interpreter.get_tensor(output_idx)
        
        # 记录预测的类别 (0, 1, 2, 3)
        preds.append(np.argmax(output_data))
        
        if i % 75 == 0:
            print(f"\r进度: {i}/{len(X_test)}", end="")
    print("\n推理完成!")
    return np.array(preds)

def plot_interactive_result(X_test, y_test, y_pred):
    """交互式可视化函数"""
    fig, ax = plt.subplots(figsize=(14, 7))
    
    # 限制显示数量以保持界面流畅
    display_limit = 12000
    indices = np.arange(min(len(y_test), display_limit))
    
    y_true_disp = y_test[indices]
    y_pred_disp = y_pred[indices]
    X_disp = X_test[indices]

    # 绘制
    ax.plot(indices, y_true_disp, 'go-', alpha=0.5, label='True Label', picker=5)
    ax.plot(indices, y_pred_disp, 'rx--', alpha=0.5, label='Model Prediction')
    
    # 标记错误
    errors = y_true_disp != y_pred_disp
    ax.scatter(indices[errors], y_pred_disp[errors], color='purple', s=75, zorder=5, label='Error Point', picker=5)

    ax.set_yticks(list(CLASS_MAP.keys()))
    ax.set_yticklabels(list(CLASS_MAP.values()))
    ax.set_title(f"TFLite Model Evaluation (Accuracy: {np.mean(y_test == y_pred):.2%})\nClick on points to view sensor data")
    ax.legend()
    ax.grid(True, axis='y', linestyle='--')

    # 点击事件
    def on_click(event):
        if event.inaxes == ax and event.button == 1:
            idx = int(round(event.xdata))
            if 0 <= idx < len(indices):
                sample_data = X_disp[idx] # (75, 12)
                
                # 创建子弹窗显示波形
                sub_fig = plt.figure(figsize=(8, 4))
                sub_fig.canvas.manager.set_window_title(f"Sample #{idx} Waveform")
                sub_ax = sub_fig.add_subplot(111)
                
                # 绘制关键特征 (Roll/Pitch)
                try:
                    roll_idx = FEATURE_COLUMNS.index('roll')
                    pitch_idx = FEATURE_COLUMNS.index('pitch')
                    acc_z_idx = FEATURE_COLUMNS.index('accel_z')
                    
                    sub_ax.plot(sample_data[:, roll_idx], label='Roll', color='blue')
                    sub_ax.plot(sample_data[:, pitch_idx], label='Pitch', color='orange')
                    sub_ax.plot(sample_data[:, acc_z_idx], label='Acc Z', color='green', alpha=0.3)
                except:
                    sub_ax.plot(sample_data[:, 0], label='Feat 0')
                
                status_text = "CORRECT" if y_true_disp[idx] == y_pred_disp[idx] else "ERROR"
                sub_ax.set_title(f"Idx: {idx} | True: {CLASS_MAP[y_true_disp[idx]]} | Pred: {CLASS_MAP[y_pred_disp[idx]]} [{status_text}]")
                sub_ax.legend()
                sub_fig.tight_layout()
                sub_fig.show()

    fig.canvas.mpl_connect('button_press_event', on_click)
    plt.show()

if __name__ == "__main__":
    if not os.path.exists(TFLITE_MODEL_PATH):
        print(f"❌ 找不到模型文件: {TFLITE_MODEL_PATH}")
        print("请先运行训练脚本生成 .tflite 文件")
    else:
        # 1. 还原数据
        X_test, y_test = load_data_and_recreate_test_set()
        
        if X_test is not None:
            # 2. 运行 TFLite
            y_pred = run_tflite_inference(TFLITE_MODEL_PATH, X_test)
            
            # 3. 打印统计
            if y_pred is not None:
                acc = np.mean(y_test == y_pred)
                print(f"\n📊 最终准确率: {acc:.2%}")
                
                # 4. 可视化
                print("正在打开可视化窗口...")
                plot_interactive_result(X_test, y_test, y_pred)