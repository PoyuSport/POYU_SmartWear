

# # -*- coding: utf-8 -*-
# import tensorflow as tf
# from tensorflow.keras import layers
# import numpy as np
# import pandas as pd
# # 引入 GroupShuffleSplit
# from sklearn.model_selection import train_test_split
# import tensorflow_model_optimization as tfmot
# import glob
# import matplotlib.pyplot as plt
# import os

# # 硬件加速配置（可选）
# physical_devices = tf.config.list_physical_devices('GPU')
# if len(physical_devices) > 0:
#     tf.config.experimental.set_memory_growth(physical_devices[0], True)

# # 类别定义
# CLASS_MAP = {
#     0: "其他",
#     1: "步行",
#     2: "骑行",
#     3: "跑步"
# }

# # 标准特征定义（模型输入层对应的名称）
# FEATURE_COLUMNS = [
#     'roll', 'pitch', 'yaw',                
#     'accel_x', 'accel_y', 'accel_z',       
#     'gyro_x', 'gyro_y', 'gyro_z',          
#     'vert_line_accel', 'accel_mag',        
#     'hor_accel', 'vert_percent'            
# ]
# assert len(FEATURE_COLUMNS) == 13, "特征数量必须为13个"

# def normalize_columns(csv_columns):
#     """
#     【新增功能】智能列名映射器
#     解决 CSV 表头被截断的问题（如 vert_perce -> vert_percent）
#     """
#     mapping = {}
#     missing = []
    
#     # 清理 CSV 列名（去空格、转小写以便匹配）
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

# def build_prunable_hybrid_model(input_shape, num_classes):
#     """构建模型结构"""
#     inputs = layers.Input(shape=input_shape)

#     # LSTM分支
#     x_lstm = layers.LSTM(32, 
#                         return_sequences=True,
#                         activation='tanh',
#                         recurrent_activation='sigmoid',
#                         unroll=True,
#                         kernel_regularizer='l2')(inputs)
#     x_lstm = layers.GlobalAveragePooling1D()(x_lstm)

#     # CNN分支
#     x_cnn = layers.Conv1D(16, 5, activation='relu')(inputs)
#     x_cnn = layers.MaxPooling1D(2)(x_cnn)
#     x_cnn = layers.Flatten()(x_cnn)

#     # 统计特征分支
#     x_stat = layers.GlobalAveragePooling1D()(inputs)
#     x_stat_var = layers.Lambda(lambda x: tf.math.reduce_std(x, axis=1))(inputs)
#     merged_stat = layers.Concatenate()([x_stat, x_stat_var])

#     # 融合
#     merged = layers.Concatenate()([x_lstm, x_cnn, merged_stat])
#     merged = layers.Dense(64, activation='relu', kernel_regularizer='l2')(merged)
#     merged = layers.Dropout(0.3)(merged)

#     outputs = layers.Dense(num_classes, activation='softmax', name='output_layer')(merged)
#     return tf.keras.Model(inputs, outputs)

# def apply_pruning(model):
#     """剪枝配置"""
#     pruning_params = {
#         'pruning_schedule': tfmot.sparsity.keras.PolynomialDecay(
#             initial_sparsity=0.05,
#             final_sparsity=0.10,
#             begin_step=2000,
#             end_step=5000
#         ),
#     }
#     return tfmot.sparsity.keras.prune_low_magnitude(
#         model,
#         **pruning_params,
#         exclude_layers=lambda layer: isinstance(layer, layers.LSTM)
#     )

# def create_time_windows(data, labels, sample_ids, window_size=75, step=40):
#     """窗口切分"""
#     windows = []
#     window_labels = []
#     window_groups = []

#     for start in range(0, len(data) - window_size + 1, step):
#         end = start + window_size
#         window = data[start:end]
#         current_sample_id = sample_ids[start]
        
#         if np.all(sample_ids[start:end] == current_sample_id):
#             windows.append(window)
#             current_labels = labels[start:end]
#             if len(np.unique(current_labels)) == 1:
#                 window_labels.append(current_labels[0])
#                 window_groups.append(current_sample_id)
    
#     return (np.array(windows, dtype=np.float32), 
#             np.array(window_labels, dtype=np.int32), 
#             np.array(window_groups, dtype=np.int32))

# def normalize_features(X):
#     """
#     X: np.ndarray, shape = (N, T, 13) or (N, 13)
#     对齐 MCU 端的手工归一化逻辑
#     """
#     X = X.astype(np.float32).copy()

#     def idx(name):
#         return FEATURE_COLUMNS.index(name)

#     # -------- 姿态角（单位：度）---------
#     X[..., idx('roll')]  /= 180.0    # [-180, 180] → ~[-1, 1]
#     X[..., idx('pitch')] /= 90.0     # [-90, 90]   → ~[-1, 1]
#     X[..., idx('yaw')]   /= 180.0    # [-180, 180] → ~[-1, 1]

#     # -------- 加速度（单位：g，量程 ±16g）---------
#     X[..., idx('accel_x')] /= 16.0
#     X[..., idx('accel_y')] /= 16.0
#     X[..., idx('accel_z')] /= 16.0

#     # -------- 陀螺仪（单位：dps，量程 ±2000）---------
#     X[..., idx('gyro_x')] /= 2000.0
#     X[..., idx('gyro_y')] /= 2000.0
#     X[..., idx('gyro_z')] /= 2000.0

#     # -------- 派生特征（极其重要）---------
#     # ✅ 必须与你 MCU 里的真实物理定义一致

#     # vert_line_accel:
#     # 如果它本身就是 accel_z 或类似 g 值（±16g）
#     X[..., idx('vert_line_accel')] /= 16.0

#     # accel_mag:
#     # √(ax² + ay² + az²)，最大约 √(3 * 16²) ≈ 27.7
#     X[..., idx('accel_mag')] /= 30.0   # 留安全裕量

#     # hor_accel:
#     # √(ax² + ay²)，最大约 √(2 * 16²) ≈ 22.6
#     X[..., idx('hor_accel')] /= 25.0

#     return X

# def plot_static_comparison(test_samples, true_labels, pred_labels):
#     """
#     可视化对比 - 增强版 (支持点击查看 Pitch/Roll 曲线)
#     """
#     fig, main_ax = plt.subplots(figsize=(15, 8))
    
#     # 限制显示数量，防止UI卡顿
#     limit = 500
#     if len(true_labels) > limit:
#         display_true = true_labels[:limit]
#         display_pred = pred_labels[:limit]
#         # 同样需要切分样本数据，保证索引对应
#         display_samples = test_samples[:limit] 
#         sample_indices = np.arange(limit)
#     else:
#         display_true = true_labels
#         display_pred = pred_labels
#         display_samples = test_samples
#         sample_indices = np.arange(len(true_labels))
    
#     # 绘制主图
#     main_ax.plot(sample_indices, display_true, 'g-o', alpha=0.6, label='True Labels', picker=5)
#     main_ax.plot(sample_indices, display_pred, 'r--x', alpha=0.6, label='Predicted Labels')
    
#     # 标记错误点
#     error_mask = display_true != display_pred
#     main_ax.scatter(sample_indices[error_mask], display_pred[error_mask],
#                     color='purple', s=80,  picker=5 ,edgecolor='black', zorder=3, label='Errors')

#     main_ax.set_yticks(list(CLASS_MAP.keys()))
#     main_ax.set_yticklabels([f'{k}: {v}' for k, v in CLASS_MAP.items()])
#     plt.title(f'Evaluation Results (First {len(display_true)} samples)\nAccuracy: {np.mean(~error_mask):.2%}\n[Hint: Click on the green line/dots to see Pitch/Roll data]')
#     main_ax.legend()
    
#     # --- 交互逻辑 ---
#     def on_click(event):
#         # 确保点击的是主坐标轴且是左键
#         if event.inaxes == main_ax and event.button == 1:
#             # 获取点击的 x 坐标（即样本索引）
#             idx = int(round(event.xdata))
            
#             if 0 <= idx < len(display_samples):
#                 # 获取该样本的 75 个时间步数据
#                 current_sample_data = display_samples[idx] # shape: (75, 13)
                
#                 # 获取特征索引 (假设 FEATURE_COLUMNS 是全局变量)
#                 try:
#                     idx_roll = FEATURE_COLUMNS.index('roll')
#                     idx_pitch = FEATURE_COLUMNS.index('pitch')
#                     # 你可以在这里添加更多想看的特征，比如 'accel_z'
#                     # idx_accel_z = FEATURE_COLUMNS.index('accel_z')
#                 except ValueError:
#                     print("特征名称未找到")
#                     return

#                 # 提取曲线
#                 series_roll = current_sample_data[:, idx_roll]
#                 series_pitch = current_sample_data[:, idx_pitch]
                
#                 # --- 弹出新窗口绘制曲线 ---
#                 # 注意：某些 IDE 配置下，新窗口可能不会立即弹出或被遮挡
#                 sub_fig = plt.figure(figsize=(10, 4))
#                 sub_fig.canvas.manager.set_window_title(f"Sample #{idx}")
#                 sub_ax = sub_fig.add_subplot(111)
                
#                 sub_ax.plot(series_roll, label='Roll', color='blue', linewidth=2)
#                 sub_ax.plot(series_pitch, label='Pitch', color='orange', linewidth=2)
                
#                 # 设置标题信息
#                 t_label = CLASS_MAP.get(display_true[idx], "Unknown")
#                 p_label = CLASS_MAP.get(display_pred[idx], "Unknown")
#                 status = "CORRECT" if display_true[idx] == display_pred[idx] else "WRONG"
                
#                 sub_ax.set_title(f"Sample #{idx} Detail [{status}]\nTrue: {t_label} | Pred: {p_label}")
#                 sub_ax.set_xlabel("Time Steps (0-99)")
#                 sub_ax.set_ylabel("Sensor Value")
#                 sub_ax.grid(True, linestyle='--', alpha=0.7)
#                 sub_ax.legend()
                
#                 sub_fig.tight_layout()
#                 sub_fig.show() # 显示子弹窗

#     # 绑定点击事件
#     fig.canvas.mpl_connect('button_press_event', on_click)
#     plt.show()

# def main():
#     root_path = './train_data/' 
#     file_list = glob.glob(root_path + '**/*.[cC][sS][vV]', recursive=True)

#     if not file_list:
#         print(f"错误：在 {root_path} 下未找到任何 CSV 文件")
#         return

#     print(f"找到 {len(file_list)} 个文件，开始处理...")
    
#     all_data = []
#     global_max_sample_id = -1

#     for file_path in file_list:
#         try:
#             header_df = pd.read_csv(file_path, nrows=0)
#             csv_columns = header_df.columns.tolist()
#             col_map, missing = normalize_columns(csv_columns)
            
#             if missing: continue

#             cols_to_read = list(col_map.values())
#             label_col = next((c for c in csv_columns if 'Train_Label' in c or 'Train_Labe' in c), None)
#             block_col = next((c for c in csv_columns if 'Sample_Block' in c), None)
            
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

#         except Exception as e:
#             print(f"读取文件失败: {e}")
#             continue

#     if not all_data: return

#     combined_data = pd.concat(all_data, ignore_index=True)
#     feature_data = combined_data[FEATURE_COLUMNS].values.astype(np.float32)
#     sample_ids = combined_data['sample_id'].values.astype(np.int32)
#     labels = combined_data['Train_Label'].values.astype(np.int32)

#     X, y, groups = create_time_windows(feature_data, labels, sample_ids, window_size=75, step=40)
#     # X = normalize_features(X)    

#     if len(X) == 0: return

#     print(f"数据准备就绪：X={X.shape}, y={y.shape}")
#     print("使用 GroupShuffleSplit 进行数据集划分...")
#     # 数据集切分
#     X_train, X_test, y_train, y_test = train_test_split(
#         X, y, test_size=0.2, random_state=42, stratify=y
#     )
    
#     print(f"训练集大小: {X_train.shape}, 随机验证集大小: {X_test.shape}")

#     base_model = build_prunable_hybrid_model(input_shape=(75, 13), num_classes=4)
#     pruned_model = apply_pruning(base_model)

#     pruned_model.compile(
#         optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
#         loss='sparse_categorical_crossentropy',
#         metrics=['accuracy']
#     )

#     callbacks = [
#         tfmot.sparsity.keras.UpdatePruningStep(),
#         tf.keras.callbacks.EarlyStopping(patience=8, restore_best_weights=True)
#     ]

#     pruned_model.fit(
#         X_train, y_train,
#         validation_data=(X_test, y_test),
#         epochs=30, 
#         batch_size=32,
#         callbacks=callbacks,
#         verbose=1
#     )

#     final_model = tfmot.sparsity.keras.strip_pruning(pruned_model)
#     final_model.save('trained_model_13feat.h5')

#     # 可视化结果（这里会调用包含交互的新函数）
#     pred_probs = final_model.predict(X_test)
#     pred_labels = np.argmax(pred_probs, axis=1)
    
#     # 【重要】 这里传入 X_test 以便绘图函数可以读取原始数据
#     plot_static_comparison(X_test, y_test, pred_labels)

#     # TFLite 转换部分略（与之前一致）
#     print("开始 TFLite 转换...")
#     # ...后续转换代码保持原样...
#     # (为节省篇幅，此处省略转换代码，你的实际运行中保留即可)
#     def representative_dataset():
#         n_samples = min(len(X_train), 500)
#         indices = np.random.choice(len(X_train), n_samples, replace=False)
#         for i in indices:
#             yield [X_train[i].reshape(1, 75, 13).astype(np.float32)]

#     converter = tf.lite.TFLiteConverter.from_keras_model(final_model)
#     converter.optimizations = [tf.lite.Optimize.DEFAULT]
#     converter.representative_dataset = representative_dataset
#     converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS, tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
#     converter.inference_input_type = tf.float32
#     converter.inference_output_type = tf.float32

#     # converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
#     # converter.inference_input_type = tf.int8
#     # converter.inference_output_type = tf.int8

#     # converter._experimental_lower_tensor_list_ops = False 

#     try:
#         tflite_model = converter.convert()
#         # 保存文件
#         out_name = "model_13feat.tflite"
#         with open(out_name, 'wb') as f:
#             f.write(tflite_model)
            
#         # 生成 C 头文件
#         hex_content = ', '.join([f'0x{b:02x}' for b in tflite_model])
#         c_header = f"""
# // Generated by XSimple Assistant
# // Model supports: FLOAT32 Input -> INT8 Ops -> FLOAT32 Output
# #ifndef MODEL_H
# #define MODEL_H

# #include <stdint.h>

# const unsigned char model_data[] __attribute__((aligned(8))) = {{
#   {hex_content}
# }};
# const int model_data_len = {len(tflite_model)};

# #endif
#         """
#         with open("model_io_float32.h", "w") as f:
#             f.write(c_header)
            
#         print(f"转换成功! 模型大小: {len(tflite_model)/1024:.2f} KB")
#         print("已生成 model_io_float32.h (可以直接拷贝 float 数组到 input tensor)")

#     except Exception as e:
#         print(f"TFLite 转换失败: {e}")

# if __name__ == "__main__":
#     main()

###################################################################################################################################

# # -*- coding: utf-8 -*-
# import tensorflow as tf
# from tensorflow.keras import layers
# import numpy as np
# import pandas as pd
# # 引入 GroupShuffleSplit
# from sklearn.model_selection import train_test_split, GroupShuffleSplit 
# import tensorflow_model_optimization as tfmot
# import glob
# import matplotlib.pyplot as plt
# import os

# # 硬件加速配置（可选）
# physical_devices = tf.config.list_physical_devices('GPU')
# if len(physical_devices) > 0:
#     tf.config.experimental.set_memory_growth(physical_devices[0], True)

# # 类别定义
# CLASS_MAP = {
#     0: "其他",
#     1: "步行",
#     2: "骑行",
#     3: "跑步"
# }

# # 标准特征定义（模型输入层对应的名称）
# FEATURE_COLUMNS = [
#     'roll', 'pitch', 'yaw',                
#     'accel_x', 'accel_y', 'accel_z',       
#     'gyro_x', 'gyro_y', 'gyro_z',          
#     'vert_line_accel', 'accel_mag',        
#     'hor_accel', 'vert_percent'            
# ]
# assert len(FEATURE_COLUMNS) == 13, "特征数量必须为13个"

# def normalize_columns(csv_columns):
#     """
#     【新增功能】智能列名映射器
#     解决 CSV 表头被截断的问题（如 vert_perce -> vert_percent）
#     """
#     mapping = {}
#     missing = []
    
#     # 清理 CSV 列名（去空格、转小写以便匹配）
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

# def build_prunable_hybrid_model(input_shape, num_classes):
#     """构建模型结构"""
#     inputs = layers.Input(shape=input_shape)

#     # LSTM分支
#     x_lstm = layers.LSTM(32, 
#                         return_sequences=True,
#                         activation='tanh',
#                         recurrent_activation='sigmoid',
#                         unroll=True,
#                         kernel_regularizer='l2')(inputs)
#     x_lstm = layers.GlobalAveragePooling1D()(x_lstm)

#     # CNN分支
#     x_cnn = layers.Conv1D(16, 5, activation='relu')(inputs)
#     x_cnn = layers.MaxPooling1D(2)(x_cnn)
#     x_cnn = layers.Flatten()(x_cnn)

#     # 统计特征分支
#     x_stat = layers.GlobalAveragePooling1D()(inputs)
#     x_stat_var = layers.Lambda(lambda x: tf.math.reduce_std(x, axis=1))(inputs)
#     merged_stat = layers.Concatenate()([x_stat, x_stat_var])

#     # 融合
#     merged = layers.Concatenate()([x_lstm, x_cnn, merged_stat])
#     merged = layers.Dense(64, activation='relu', kernel_regularizer='l2')(merged)
#     merged = layers.Dropout(0.3)(merged)

#     outputs = layers.Dense(num_classes, activation='softmax', name='output_layer')(merged)
#     return tf.keras.Model(inputs, outputs)

# def apply_pruning(model):
#     """剪枝配置"""
#     pruning_params = {
#         'pruning_schedule': tfmot.sparsity.keras.PolynomialDecay(
#             initial_sparsity=0.05,
#             final_sparsity=0.10,
#             begin_step=2000,
#             end_step=5000
#         ),
#     }
#     return tfmot.sparsity.keras.prune_low_magnitude(
#         model,
#         **pruning_params,
#         exclude_layers=lambda layer: isinstance(layer, layers.LSTM)
#     )

# def create_time_windows(data, labels, sample_ids, window_size=75, step=40):
#     """窗口切分"""
#     windows = []
#     window_labels = []
#     window_groups = []

#     for start in range(0, len(data) - window_size + 1, step):
#         end = start + window_size
#         window = data[start:end]
#         current_sample_id = sample_ids[start]
        
#         if np.all(sample_ids[start:end] == current_sample_id):
#             windows.append(window)
#             current_labels = labels[start:end]
#             if len(np.unique(current_labels)) == 1:
#                 window_labels.append(current_labels[0])
#                 window_groups.append(current_sample_id)
    
#     return (np.array(windows, dtype=np.float32), 
#             np.array(window_labels, dtype=np.int32), 
#             np.array(window_groups, dtype=np.int32))

# def normalize_features(X):
#     """
#     X: np.ndarray, shape = (N, T, 13) or (N, 13)
#     对齐 MCU 端的手工归一化逻辑
#     """
#     X = X.astype(np.float32).copy()

#     def idx(name):
#         return FEATURE_COLUMNS.index(name)

#     # -------- 姿态角（单位：度）---------
#     X[..., idx('roll')]  /= 180.0    # [-180, 180] → ~[-1, 1]
#     X[..., idx('pitch')] /= 90.0     # [-90, 90]   → ~[-1, 1]
#     X[..., idx('yaw')]   /= 180.0    # [-180, 180] → ~[-1, 1]

#     # -------- 加速度（单位：g，量程 ±16g）---------
#     X[..., idx('accel_x')] /= 16.0
#     X[..., idx('accel_y')] /= 16.0
#     X[..., idx('accel_z')] /= 16.0

#     # -------- 陀螺仪（单位：dps，量程 ±2000）---------
#     X[..., idx('gyro_x')] /= 2000.0
#     X[..., idx('gyro_y')] /= 2000.0
#     X[..., idx('gyro_z')] /= 2000.0

#     # -------- 派生特征（极其重要）---------
#     # ✅ 必须与你 MCU 里的真实物理定义一致

#     # vert_line_accel:
#     # 如果它本身就是 accel_z 或类似 g 值（±16g）
#     X[..., idx('vert_line_accel')] /= 16.0

#     # accel_mag:
#     # √(ax² + ay² + az²)，最大约 √(3 * 16²) ≈ 27.7
#     X[..., idx('accel_mag')] /= 30.0   # 留安全裕量

#     # hor_accel:
#     # √(ax² + ay²)，最大约 √(2 * 16²) ≈ 22.6
#     X[..., idx('hor_accel')] /= 25.0

#     return X

# def plot_static_comparison(test_samples, true_labels, pred_labels):
#     """
#     可视化对比 - 增强版 (支持点击查看 Pitch/Roll 曲线)
#     """
#     fig, main_ax = plt.subplots(figsize=(15, 8))
    
#     # 限制显示数量，防止UI卡顿
#     limit = 3000
#     if len(true_labels) > limit:
#         display_true = true_labels[:limit]
#         display_pred = pred_labels[:limit]
#         # 同样需要切分样本数据，保证索引对应
#         display_samples = test_samples[:limit] 
#         sample_indices = np.arange(limit)
#     else:
#         display_true = true_labels
#         display_pred = pred_labels
#         display_samples = test_samples
#         sample_indices = np.arange(len(true_labels))
    
#     # 绘制主图
#     main_ax.plot(sample_indices, display_true, 'g-o', alpha=0.6, label='True Labels', picker=5)
#     main_ax.plot(sample_indices, display_pred, 'r--x', alpha=0.6, label='Predicted Labels')
    
#     # 标记错误点
#     error_mask = display_true != display_pred
#     main_ax.scatter(sample_indices[error_mask], display_pred[error_mask],
#                     color='purple', s=80,  picker=5 ,edgecolor='black', zorder=3, label='Errors')

#     main_ax.set_yticks(list(CLASS_MAP.keys()))
#     main_ax.set_yticklabels([f'{k}: {v}' for k, v in CLASS_MAP.items()])
#     plt.title(f'Evaluation Results (First {len(display_true)} samples)\nAccuracy: {np.mean(~error_mask):.2%}\n[Hint: Click on the green line/dots to see Pitch/Roll data]')
#     main_ax.legend()
    
#     # --- 交互逻辑 ---
#     def on_click(event):
#         # 确保点击的是主坐标轴且是左键
#         if event.inaxes == main_ax and event.button == 1:
#             # 获取点击的 x 坐标（即样本索引）
#             idx = int(round(event.xdata))
            
#             if 0 <= idx < len(display_samples):
#                 # 获取该样本的 75 个时间步数据
#                 current_sample_data = display_samples[idx] # shape: (75, 13)
                
#                 # 获取特征索引 (假设 FEATURE_COLUMNS 是全局变量)
#                 try:
#                     idx_roll = FEATURE_COLUMNS.index('roll')
#                     idx_pitch = FEATURE_COLUMNS.index('pitch')
#                     # 你可以在这里添加更多想看的特征，比如 'accel_z'
#                     # idx_accel_z = FEATURE_COLUMNS.index('accel_z')
#                 except ValueError:
#                     print("特征名称未找到")
#                     return

#                 # 提取曲线
#                 series_roll = current_sample_data[:, idx_roll]
#                 series_pitch = current_sample_data[:, idx_pitch]
                
#                 # --- 弹出新窗口绘制曲线 ---
#                 # 注意：某些 IDE 配置下，新窗口可能不会立即弹出或被遮挡
#                 sub_fig = plt.figure(figsize=(10, 4))
#                 sub_fig.canvas.manager.set_window_title(f"Sample #{idx}")
#                 sub_ax = sub_fig.add_subplot(111)
                
#                 sub_ax.plot(series_roll, label='Roll', color='blue', linewidth=2)
#                 sub_ax.plot(series_pitch, label='Pitch', color='orange', linewidth=2)
                
#                 # 设置标题信息
#                 t_label = CLASS_MAP.get(display_true[idx], "Unknown")
#                 p_label = CLASS_MAP.get(display_pred[idx], "Unknown")
#                 status = "CORRECT" if display_true[idx] == display_pred[idx] else "WRONG"
                
#                 sub_ax.set_title(f"Sample #{idx} Detail [{status}]\nTrue: {t_label} | Pred: {p_label}")
#                 sub_ax.set_xlabel("Time Steps (0-99)")
#                 sub_ax.set_ylabel("Sensor Value")
#                 sub_ax.grid(True, linestyle='--', alpha=0.7)
#                 sub_ax.legend()
                
#                 sub_fig.tight_layout()
#                 sub_fig.show() # 显示子弹窗

#     # 绑定点击事件
#     fig.canvas.mpl_connect('button_press_event', on_click)
#     plt.show()

# def main():
#     root_path = './train_data/' 
#     file_list = glob.glob(root_path + '**/*.[cC][sS][vV]', recursive=True)

#     if not file_list:
#         print(f"错误：在 {root_path} 下未找到任何 CSV 文件")
#         return

#     print(f"找到 {len(file_list)} 个文件，开始处理...")
    
#     all_data = []
#     global_max_sample_id = -1

#     for file_path in file_list:
#         try:
#             header_df = pd.read_csv(file_path, nrows=0)
#             csv_columns = header_df.columns.tolist()
#             col_map, missing = normalize_columns(csv_columns)
            
#             if missing: continue

#             cols_to_read = list(col_map.values())
#             label_col = next((c for c in csv_columns if 'Train_Label' in c or 'Train_Labe' in c), None)
#             block_col = next((c for c in csv_columns if 'Sample_Block' in c), None)
            
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

#         except Exception as e:
#             print(f"读取文件失败: {e}")
#             continue

#     if not all_data: return

#     combined_data = pd.concat(all_data, ignore_index=True)
#     feature_data = combined_data[FEATURE_COLUMNS].values.astype(np.float32)
#     sample_ids = combined_data['sample_id'].values.astype(np.int32)
#     labels = combined_data['Train_Label'].values.astype(np.int32)

#     X, y, groups = create_time_windows(feature_data, labels, sample_ids, window_size=75, step=40)
#     X = normalize_features(X)    

#     if len(X) == 0: return

#     print(f"数据准备就绪：X={X.shape}, y={y.shape}")
#     print("使用 GroupShuffleSplit 进行数据集划分...")
#     splitter = GroupShuffleSplit(test_size=0.2, n_splits=1, random_state=42)
    
#     train_idx, test_idx = next(splitter.split(X, y, groups=groups))
#     X_train, X_test = X[train_idx], X[test_idx]
#     y_train, y_test = y[train_idx], y[test_idx]
    
#     print(f"训练集大小: {X_train.shape}, 测试集大小: {X_test.shape}")

#     base_model = build_prunable_hybrid_model(input_shape=(75, 13), num_classes=4)
#     pruned_model = apply_pruning(base_model)

#     pruned_model.compile(
#         optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
#         loss='sparse_categorical_crossentropy',
#         metrics=['accuracy']
#     )

#     callbacks = [
#         tfmot.sparsity.keras.UpdatePruningStep(),
#         tf.keras.callbacks.EarlyStopping(patience=8, restore_best_weights=True)
#     ]

#     pruned_model.fit(
#         X_train, y_train,
#         validation_data=(X_test, y_test),
#         epochs=30, 
#         batch_size=32,
#         callbacks=callbacks,
#         verbose=1
#     )

#     final_model = tfmot.sparsity.keras.strip_pruning(pruned_model)
#     final_model.save('trained_model_13feat.h5')

#     # 可视化结果（这里会调用包含交互的新函数）
#     pred_probs = final_model.predict(X_test)
#     pred_labels = np.argmax(pred_probs, axis=1)
    
#     # 【重要】 这里传入 X_test 以便绘图函数可以读取原始数据
#     plot_static_comparison(X_test, y_test, pred_labels)

#     # TFLite 转换部分略（与之前一致）
#     print("开始 TFLite 转换...")
#     # ...后续转换代码保持原样...
#     # (为节省篇幅，此处省略转换代码，你的实际运行中保留即可)
#     def representative_dataset():
#         n_samples = min(len(X_train), 500)
#         indices = np.random.choice(len(X_train), n_samples, replace=False)
#         for i in indices:
#             yield [X_train[i].reshape(1, 75, 13).astype(np.float32)]

#     converter = tf.lite.TFLiteConverter.from_keras_model(final_model)
#     converter.optimizations = [tf.lite.Optimize.DEFAULT]
#     converter.representative_dataset = representative_dataset
#     converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
#     #converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS, tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
#     # converter.inference_input_type = tf.float32
#     # converter.inference_output_type = tf.float32

#     converter.inference_input_type = tf.int8
#     converter.inference_output_type = tf.int8

#     converter._experimental_lower_tensor_list_ops = False 

#     try:
#         tflite_model = converter.convert()
#         # 保存文件
#         out_name = "model_13feat.tflite"
#         with open(out_name, 'wb') as f:
#             f.write(tflite_model)
            
#         # 生成 C 头文件
#         hex_content = ', '.join([f'0x{b:02x}' for b in tflite_model])
#         c_header = f"""
# // Generated by XSimple Assistant
# // Model supports: INT8 Input -> INT8 Ops -> INT8 Output
# #ifndef MODEL_H
# #define MODEL_H

# #include <stdint.h>

# const unsigned char model_data[] __attribute__((aligned(8))) = {{
#   {hex_content}
# }};
# const int model_data_len = {len(tflite_model)};

# #endif
#         """
#         with open("model_io_int8.h", "w") as f:
#             f.write(c_header)
            
#         print(f"转换成功! 模型大小: {len(tflite_model)/1024:.2f} KB")
#         print("已生成 model_io_int8.h (可以直接拷贝 float 数组到 input tensor)")

#     except Exception as e:
#         print(f"TFLite 转换失败: {e}")

# if __name__ == "__main__":
#     main()


# #########################################################################################################################################################




# -*- coding: utf-8 -*-
import tensorflow as tf
from tensorflow.keras import layers
import numpy as np
import pandas as pd
# 引入 GroupShuffleSplit
from sklearn.model_selection import GroupShuffleSplit
import tensorflow_model_optimization as tfmot
import glob
import matplotlib.pyplot as plt
import os

# 硬件加速配置（可选）
physical_devices = tf.config.list_physical_devices('GPU')
if len(physical_devices) > 0:
    tf.config.experimental.set_memory_growth(physical_devices[0], True)

# 类别定义
CLASS_MAP = {
    0: "其他",
    1: "步行",
    2: "骑行",
    3: "跑步"
}

# 标准特征定义（模型输入层对应的名称）
FEATURE_COLUMNS = [
    'roll', 'pitch',               
    'accel_x', 'accel_y', 'accel_z',       
    'gyro_x', 'gyro_y', 'gyro_z',          
    'vert_line_accel', 'accel_mag',        
    'hor_accel', 'vert_percent'            
]
assert len(FEATURE_COLUMNS) == 12, "特征数量必须为12个"

def normalize_columns(csv_columns):
    """
    【新增功能】智能列名映射器
    解决 CSV 表头被截断的问题（如 vert_perce -> vert_percent）
    """
    mapping = {}
    missing = []
    
    # 清理 CSV 列名（去空格、转小写以便匹配）
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

def build_prunable_hybrid_model(input_shape, num_classes):
    """构建模型结构"""
    inputs = layers.Input(shape=input_shape)

    # LSTM分支
    x_lstm = layers.LSTM(32, 
                        return_sequences=True,
                        activation='tanh',
                        recurrent_activation='sigmoid',
                        unroll=True,
                        kernel_regularizer='l2')(inputs)
    x_lstm = layers.GlobalAveragePooling1D()(x_lstm)

    # CNN分支
    x_cnn = layers.Conv1D(16, 5, activation='relu')(inputs)
    x_cnn = layers.MaxPooling1D(2)(x_cnn)
    x_cnn = layers.Flatten()(x_cnn)

    # 统计特征分支
    x_stat = layers.GlobalAveragePooling1D()(inputs)
    x_stat_var = layers.Lambda(lambda x: tf.math.reduce_std(x, axis=1))(inputs)
    merged_stat = layers.Concatenate()([x_stat, x_stat_var])

    # 融合
    merged = layers.Concatenate()([x_lstm, x_cnn, merged_stat])
    merged = layers.Dense(64, activation='relu', kernel_regularizer='l2')(merged)
    merged = layers.Dropout(0.3)(merged)

    outputs = layers.Dense(num_classes, activation='softmax', name='output_layer')(merged)
    return tf.keras.Model(inputs, outputs)

def apply_pruning(model):
    """剪枝配置"""
    pruning_params = {
        'pruning_schedule': tfmot.sparsity.keras.PolynomialDecay(
            initial_sparsity=0.05,
            final_sparsity=0.10,
            begin_step=2000,
            end_step=5000
        ),
    }
    return tfmot.sparsity.keras.prune_low_magnitude(
        model,
        **pruning_params,
        exclude_layers=lambda layer: isinstance(layer, layers.LSTM)
    )

def create_time_windows(data, labels, sample_ids, window_size=75, step=40):
    """窗口切分"""
    windows = []
    window_labels = []
    window_groups = []

    for start in range(0, len(data) - window_size + 1, step):
        end = start + window_size
        window = data[start:end]
        current_sample_id = sample_ids[start]
        
        if np.all(sample_ids[start:end] == current_sample_id):
            windows.append(window)
            current_labels = labels[start:end]
            if len(np.unique(current_labels)) == 1:
                window_labels.append(current_labels[0])
                window_groups.append(current_sample_id)
    
    return (np.array(windows, dtype=np.float32), 
            np.array(window_labels, dtype=np.int32), 
            np.array(window_groups, dtype=np.int32))

def normalize_features(X):
    """
    X: np.ndarray, shape = (N, T, 12) or (N, 12)
    对齐 MCU 端的手工归一化逻辑
    """
    X = X.astype(np.float32).copy()

    def idx(name):
        return FEATURE_COLUMNS.index(name)

    # -------- 姿态角（单位：度）---------
    X[..., idx('roll')]  /= 180.0    # [-180, 180] → ~[-1, 1]
    X[..., idx('pitch')] /= 90.0     # [-90, 90]   → ~[-1, 1]
    #X[..., idx('yaw')]   /= 180.0    # [-180, 180] → ~[-1, 1]

    # -------- 加速度（单位：g，量程 ±16g）---------
    X[..., idx('accel_x')] /= 16.0
    X[..., idx('accel_y')] /= 16.0
    X[..., idx('accel_z')] /= 16.0

    # -------- 陀螺仪（单位：dps，量程 ±2000）---------
    X[..., idx('gyro_x')] /= 2000.0
    X[..., idx('gyro_y')] /= 2000.0
    X[..., idx('gyro_z')] /= 2000.0

    # -------- 派生特征（极其重要）---------
    # ✅ 必须与你 MCU 里的真实物理定义一致

    # vert_line_accel:
    # 如果它本身就是 accel_z 或类似 g 值（±16g）
    X[..., idx('vert_line_accel')] /= 16.0

    # accel_mag:
    # √(ax² + ay² + az²)，最大约 √(3 * 16²) ≈ 27.7
    X[..., idx('accel_mag')] /= 30.0   # 留安全裕量

    # hor_accel:
    # √(ax² + ay²)，最大约 √(2 * 16²) ≈ 22.6
    X[..., idx('hor_accel')] /= 25.0

    return X

def plot_static_comparison(test_samples, true_labels, pred_labels):
    """
    可视化对比 - 增强版 (支持点击查看 Pitch/Roll 曲线)
    """
    fig, main_ax = plt.subplots(figsize=(15, 8))
    
    # 限制显示数量，防止UI卡顿
    limit = 500
    if len(true_labels) > limit:
        display_true = true_labels[:limit]
        display_pred = pred_labels[:limit]
        # 同样需要切分样本数据，保证索引对应
        display_samples = test_samples[:limit] 
        sample_indices = np.arange(limit)
    else:
        display_true = true_labels
        display_pred = pred_labels
        display_samples = test_samples
        sample_indices = np.arange(len(true_labels))
    
    # 绘制主图
    main_ax.plot(sample_indices, display_true, 'g-o', alpha=0.6, label='True Labels', picker=5)
    main_ax.plot(sample_indices, display_pred, 'r--x', alpha=0.6, label='Predicted Labels')
    
    # 标记错误点
    error_mask = display_true != display_pred
    main_ax.scatter(sample_indices[error_mask], display_pred[error_mask],
                    color='purple', s=80,  picker=5 ,edgecolor='black', zorder=3, label='Errors')

    main_ax.set_yticks(list(CLASS_MAP.keys()))
    main_ax.set_yticklabels([f'{k}: {v}' for k, v in CLASS_MAP.items()])
    plt.title(f'Evaluation Results (First {len(display_true)} samples)\nAccuracy: {np.mean(~error_mask):.2%}\n[Hint: Click on the green line/dots to see Pitch/Roll data]')
    main_ax.legend()
    
    # --- 交互逻辑 ---
    def on_click(event):
        # 确保点击的是主坐标轴且是左键
        if event.inaxes == main_ax and event.button == 1:
            # 获取点击的 x 坐标（即样本索引）
            idx = int(round(event.xdata))
            
            if 0 <= idx < len(display_samples):
                # 获取该样本的 75 个时间步数据
                current_sample_data = display_samples[idx] # shape: (75, 12)
                
                # 获取特征索引 (假设 FEATURE_COLUMNS 是全局变量)
                try:
                    idx_roll = FEATURE_COLUMNS.index('roll')
                    idx_pitch = FEATURE_COLUMNS.index('pitch')
                    # 你可以在这里添加更多想看的特征，比如 'accel_z'
                    # idx_accel_z = FEATURE_COLUMNS.index('accel_z')
                except ValueError:
                    print("特征名称未找到")
                    return

                # 提取曲线
                series_roll = current_sample_data[:, idx_roll]
                series_pitch = current_sample_data[:, idx_pitch]
                
                # --- 弹出新窗口绘制曲线 ---
                # 注意：某些 IDE 配置下，新窗口可能不会立即弹出或被遮挡
                sub_fig = plt.figure(figsize=(10, 4))
                sub_fig.canvas.manager.set_window_title(f"Sample #{idx}")
                sub_ax = sub_fig.add_subplot(111)
                
                sub_ax.plot(series_roll, label='Roll', color='blue', linewidth=2)
                sub_ax.plot(series_pitch, label='Pitch', color='orange', linewidth=2)
                
                # 设置标题信息
                t_label = CLASS_MAP.get(display_true[idx], "Unknown")
                p_label = CLASS_MAP.get(display_pred[idx], "Unknown")
                status = "CORRECT" if display_true[idx] == display_pred[idx] else "WRONG"
                
                sub_ax.set_title(f"Sample #{idx} Detail [{status}]\nTrue: {t_label} | Pred: {p_label}")
                sub_ax.set_xlabel("Time Steps (0-99)")
                sub_ax.set_ylabel("Sensor Value")
                sub_ax.grid(True, linestyle='--', alpha=0.7)
                sub_ax.legend()
                
                sub_fig.tight_layout()
                sub_fig.show() # 显示子弹窗

    # 绑定点击事件
    fig.canvas.mpl_connect('button_press_event', on_click)
    plt.show()

def main():
    root_path = os.environ.get('TRAIN_DATA', './train_data')
    file_list = glob.glob(os.path.join(root_path, '**', '*.[cC][sS][vV]'), recursive=True)

    if not file_list:
        print(f"错误：在 {root_path} 下未找到任何 CSV 文件")
        return

    print(f"找到 {len(file_list)} 个文件，开始处理...")
    
    all_data = []
    global_max_sample_id = -1

    for file_path in file_list:
        try:
            header_df = pd.read_csv(file_path, nrows=0,low_memory=False)
            csv_columns = header_df.columns.tolist()
            col_map, missing = normalize_columns(csv_columns)
            
            if missing: continue

            cols_to_read = list(col_map.values())
            label_col = next((c for c in csv_columns if 'Train_Label' in c or 'Train_Labe' in c), None)
            block_col = next((c for c in csv_columns if 'Sample_Block' in c), None)
            
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

        except Exception as e:
            print(f"读取文件失败: {e}")
            continue

    if not all_data: return

    combined_data = pd.concat(all_data, ignore_index=True)
    feature_data = combined_data[FEATURE_COLUMNS].values.astype(np.float32)
    sample_ids = combined_data['sample_id'].values.astype(np.int32)
    labels = combined_data['Train_Label'].values.astype(np.int32)

    X, y, groups = create_time_windows(feature_data, labels, sample_ids, window_size=75, step=40)
    # X = normalize_features(X)    

    if len(X) == 0: return

    print(f"数据准备就绪：X={X.shape}, y={y.shape}")
    print("按 Sample_Block 分组划分训练/测试集...")
    # 同一采集块内的窗口高度相关，必须按块划分，避免数据泄漏
    splitter = GroupShuffleSplit(n_splits=1, test_size=0.2, random_state=42)
    train_idx, test_idx = next(splitter.split(X, y, groups))
    X_train, X_test = X[train_idx], X[test_idx]
    y_train, y_test = y[train_idx], y[test_idx]

    print(f"训练集大小: {X_train.shape}, 验证集大小: {X_test.shape}")

    base_model = build_prunable_hybrid_model(input_shape=(75, 12), num_classes=4)
    pruned_model = apply_pruning(base_model)

    pruned_model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
        loss='sparse_categorical_crossentropy',
        metrics=['accuracy']
    )

    callbacks = [
        tfmot.sparsity.keras.UpdatePruningStep(),
        tf.keras.callbacks.EarlyStopping(patience=8, restore_best_weights=True)
    ]

    pruned_model.fit(
        X_train, y_train,
        validation_data=(X_test, y_test),
        epochs=30, 
        batch_size=32,
        callbacks=callbacks,
        verbose=1
    )

    final_model = tfmot.sparsity.keras.strip_pruning(pruned_model)
    final_model.save('trained_model_12feat.h5')

    # 可视化结果（这里会调用包含交互的新函数）
    pred_probs = final_model.predict(X_test)
    pred_labels = np.argmax(pred_probs, axis=1)
    
    # 【重要】 这里传入 X_test 以便绘图函数可以读取原始数据
    plot_static_comparison(X_test, y_test, pred_labels)

    # TFLite 转换部分略（与之前一致）
    print("开始 TFLite 转换...")
    # ...后续转换代码保持原样...
    # (为节省篇幅，此处省略转换代码，你的实际运行中保留即可)
    def representative_dataset():
        n_samples = min(len(X_train), 500)
        indices = np.random.choice(len(X_train), n_samples, replace=False)
        for i in indices:
            yield [X_train[i].reshape(1, 75, 12).astype(np.float32)]

    converter = tf.lite.TFLiteConverter.from_keras_model(final_model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS, tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.float32
    converter.inference_output_type = tf.float32
    converter.experimental_enable_resource_variables = False

    # converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    # converter.inference_input_type = tf.int8
    # converter.inference_output_type = tf.int8

    # converter._experimental_lower_tensor_list_ops = False 

    try:
        tflite_model = converter.convert()
        # 保存文件
        out_name = "model_12feat.tflite"
        with open(out_name, 'wb') as f:
            f.write(tflite_model)
            
        # 生成 C 头文件
        hex_content = ', '.join([f'0x{b:02x}' for b in tflite_model])
        c_header = f"""
// Generated by XSimple Assistant
// Model supports: FLOAT32 Input -> INT8 Ops -> FLOAT32 Output
#ifndef MODEL_H
#define MODEL_H

#include <stdint.h>

const unsigned char model_data[] __attribute__((aligned(8))) = {{
  {hex_content}
}};
const int model_data_len = {len(tflite_model)};

#endif
        """
        with open("model_io12_float32.h", "w") as f:
            f.write(c_header)
            
        print(f"转换成功! 模型大小: {len(tflite_model)/1024:.2f} KB")
        print("已生成 model_io12_float32.h (可以直接拷贝 float 数组到 input tensor)")

    except Exception as e:
        print(f"TFLite 转换失败: {e}")

if __name__ == "__main__":
    main()

