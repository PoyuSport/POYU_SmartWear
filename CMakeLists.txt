cmake_minimum_required(VERSION 3.16)

# 第一步：设置目标芯片 (必须在 include 之前)
set(IDF_TARGET esp32s3)  # 确保这里使用正确的芯片型号

# 第二步：包含 IDF 项目配置
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)

# 第三步：设置ULP协处理器类型
set(ULP_COPROC_TYPE "riscv" CACHE STRING "ULP coprocessor type" FORCE)

project(smart_wear)