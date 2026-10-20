# AsyncDelay
cmake_minimum_required(VERSION 3.5)

# 注册组件 - 纯头文件库配置
idf_component_register(
    INCLUDE_DIRS "src" "."          # 指定头文件目录
    REQUIRES arduino-esp32          # 依赖项
)

# 可选：添加头文件到组件
file(GLOB HEADER_FILES "src/*.hpp" "src/*.h")
if(HEADER_FILES)
    set(COMPONENT_HEADERS ${HEADER_FILES})
endif()

project(AsyncDelay)
