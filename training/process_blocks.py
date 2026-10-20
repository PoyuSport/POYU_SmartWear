import os
import pandas as pd
import shutil
from collections import defaultdict

def process_csv_blocks(root_path, output_path=None):
    """
    遍历路径下的CSV，分析 Sample_Block，检查重复，并可选地重新编号。
    """
    
    # 存储结构：key=块号(Block ID), value=列表[包含该块号的文件的绝对路径]
    block_usage_map = defaultdict(list)
    
    # 存储结构：key=文件路径, value=该文件包含的原始块号列表(去重后)
    file_block_manifest = {}
    
    # 获取所有的 csv 文件路径
    csv_files = []
    # 使用 os.path.abspath 确保 root_path 也是绝对路径，防止相对路径拼接出问题
    abs_root_path = os.path.abspath(root_path) 
    
    for dirpath, _, filenames in os.walk(abs_root_path):
        for filename in filenames:
            if filename.lower().endswith('.csv'):
                full_path = os.path.join(dirpath, filename)
                csv_files.append(full_path)
    
    if not csv_files:
        print("未在指定路径下找到任何 .csv 文件。")
        return

    print(f"--- 此目录下共找到 {len(csv_files)} 个 CSV 文件，开始分析 ---")

    # ==============================
    # 步骤 1 & 2: 读取并记录块号
    # ==============================
    for file_path in csv_files:
        try:
            # 获取文件的绝对路径
            abs_file_path = os.path.abspath(file_path)
            
            # 只读取 Sample_Block 列以加快分析速度
            df = pd.read_csv(abs_file_path, usecols=['Sample_Block'])
            
            # 获取该文件中所有唯一的 Block ID
            unique_blocks = df['Sample_Block'].dropna().unique()
            unique_blocks.sort()
            
            # 记录: 文件 -> 块号列表
            file_block_manifest[abs_file_path] = unique_blocks
            
            # 记录: 块号 -> 文件列表 (这里存入绝对路径！)
            for block in unique_blocks:
                block_usage_map[block].append(abs_file_path)
                
            print(f"[已读取] {os.path.basename(abs_file_path)} -> 包含 {len(unique_blocks)} 个数据块")
            
        except ValueError:
            print(f"[警告] 文件 {os.path.basename(file_path)} 中未找到 'Sample_Block' 列，已跳过。")
        except Exception as e:
            print(f"[错误] 无法读取文件 {file_path}: {e}")

    # ==============================
    # 步骤 3: 检查重复块号 (根据你的要求，打印绝对路径)
    # ==============================
    print("\n" + "="*50)
    print("重复数据块检查报告")
    print("="*50)
    
    duplicates_found = False
    for block_id, files in block_usage_map.items():
        if len(files) > 1:
            duplicates_found = True
            print(f"!!! 发现重复 !!! Block ID: [{block_id}] 同时出现在以下文件中:")
            for f in files:
                # 这里的 f 已经是绝对路径了
                print(f"    - {f}")  
            print("-" * 30)
            
    if not duplicates_found:
        print("哇！没有发现任何跨文件的重复块号。")
    
    # # ==============================
    # # 步骤 4: 统一重新编号 (1 ~ N)
    # # ==============================
    # if output_path:
    #     print("\n" + "="*50)
    #     print("开始重新编号并导出文件")
    #     print("="*50)
        
    #     if not os.path.exists(output_path):
    #         os.makedirs(output_path)
            
    #     global_block_counter = 1
        
    #     # 按照文件路径顺序处理
    #     for file_path in sorted(file_block_manifest.keys()):
    #         file_name = os.path.basename(file_path)
            
    #         try:
    #             # 读取完整数据
    #             df = pd.read_csv(file_path) # file_path 已经是绝对路径
                
    #             original_blocks = file_block_manifest[file_path]
                
    #             # 建立映射字典: {旧块号: 新的全局连续块号}
    #             files_id_map = {}
    #             for old_id in original_blocks:
    #                 files_id_map[old_id] = global_block_counter
    #                 global_block_counter += 1
                
    #             # 应用映射
    #             df['Sample_Block'] = df['Sample_Block'].map(files_id_map)
                
    #             # 构建输出路径
    #             target_file_path = os.path.join(output_path, file_name)
                
    #             # 保存文件
    #             df.to_csv(target_file_path, index=False)
                
    #             print(f"[已处理] {file_name} -> ID 映射范围: {min(files_id_map.values())} ~ {max(files_id_map.values())}")
                
    #         except Exception as e:
    #             print(f"[错误] 处理文件 {file_path} 失败: {e}")
        
    #     print(f"\n处理完成！所有文件已重新编号，Block ID 从 1 到 {global_block_counter - 1}。")

# ==========================================
# 设置配置
# ==========================================
if __name__ == "__main__":
    # 请修改这里的路径
    source_directory = os.environ.get("TRAIN_DATA", "./train_data")
    target_directory = r"./csv_data_processed"
    
    process_csv_blocks(source_directory, output_path=target_directory)