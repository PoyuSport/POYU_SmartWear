Import("env")
import os
import sys
import subprocess

def build_ulp(source, target, env):
    print("="*50)
    print("开始构建 ULP 程序")
    print("="*50)
    
    # 获取项目目录
    project_dir = env['PROJECT_DIR']
    build_dir = env['PROJECT_BUILD_DIR']
    
    # 设置 ULP 工具路径
    ulp_tool_dir = env.PioPlatform().get_package_dir("toolchain-riscv32-esp")
    if not ulp_tool_dir:
        sys.stderr.write("错误: 找不到 RISC-V ULP 工具链\n")
        sys.exit(1)
    
    # 工具路径
    ulp_as = os.path.join(ulp_tool_dir, "bin", "riscv32-esp-elf-as")
    ulp_ld = os.path.join(ulp_tool_dir, "bin", "riscv32-esp-elf-ld")
    
    # ULP 源文件和输出路径
    ulp_source = os.path.join(project_dir, "ulp", "ulp_main.S")
    ulp_object = os.path.join(build_dir, "ulp_main.o")
    ulp_binary = os.path.join(build_dir, "ulp_main.bin")
    
    # 确保输出目录存在
    os.makedirs(os.path.dirname(ulp_object), exist_ok=True)
    
    # 创建命令执行函数
    def run_command(cmd):
        print(f"[CMD] {cmd}")
        try:
            result = subprocess.run(
                cmd,
                shell=True,
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding='utf-8',
                errors='replace'
            )
            print("输出:")
            print(result.stdout)
            if result.stderr:
                print("错误:")
                print(result.stderr)
            return True
        except subprocess.CalledProcessError as e:
            sys.stderr.write(f"命令失败 (code {e.returncode}):\n")
            sys.stderr.write(e.stderr.decode('utf-8', errors='replace') if isinstance(e.stderr, bytes) else e.stderr)
            sys.stderr.write("\n")
            return False
    
    # 汇编命令
    assemble_cmd = f'"{ulp_as}" -march=rv32imc -mabi=ilp32 -o "{ulp_object}" "{ulp_source}"'
    print("[ULP] 编译汇编代码...")
    if not run_command(assemble_cmd):
        sys.exit(1)
    
    # 链接命令
    ld_script = os.path.join(project_dir, "ulp", "esp32s3.ulp.ld")
    link_cmd = f'"{ulp_ld}" -T "{ld_script}" -o "{ulp_binary}" "{ulp_object}"'
    print("[ULP] 链接生成二进制文件...")
    if not run_command(link_cmd):
        sys.exit(1)
    
    # 生成头文件
    bin2header = os.path.join(ulp_tool_dir, "bin", "esp32ulp_bin2h.py")
    if os.path.exists(bin2header):
        ulp_header = os.path.join(project_dir, "src", "ulp_main.h")
        bin2header_cmd = f'python "{bin2header}" "{ulp_binary}" "{ulp_header}"'
        print("[ULP] 生成头文件 ulp_main.h...")
        run_command(bin2header_cmd)
    else:
        print(f"警告: 找不到 esp32ulp_bin2h.py")

# 在主程序编译前添加 ULP 构建步骤
env.AddPreAction("$BUILD_DIR/src/main.cpp.o", build_ulp)
