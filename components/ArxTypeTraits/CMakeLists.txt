
cmake_minimum_required(VERSION 3.5)

# 1. 注册组件但不指定源文件
idf_component_register(
    INCLUDE_DIRS "." "ArxTypeTraits"  # 指向包含头文件的目录
    # 不需要 SRCS 参数
)

# 2. 添加头文件目录为接口包含路径
target_include_directories(${COMPONENT_LIB} INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/ArxTypeTraits"
)

# 3. 可选：设置 C++ 标准（Eigen 通常需要 C++11 或更高）
target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_17)

project(ArxTypeTraits)
