cmake_minimum_required(VERSION 3.16)

# 设置ULP架构为RISC-V
set(ULP_ARCH "riscv")
# 显式添加 ULP 组件依赖
set(COMPONENTS "ulp/ulp_riscv")
# 项目名称
set(PROJECT_NAME "ulp_i2c_mpu9250")
# ULP组件配置
set(ulp_app_name ulp_${COMPONENT_NAME})  # 生成唯一的ULP应用名称
# 指定ULP C源文件（放在Ulp/目录下）
set(ulp_sources  
    "${CMAKE_CURRENT_LIST_DIR}/ulp/ulp_main.c"
)
# 列出包含ULP自动生成头文件的主程序源文件
set(ulp_exp_dep_srcs  "main.cpp")

set(ALGORITHM_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Algorithm/BodyFrameTracker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Algorithm/RPM.cpp
)

set(HARDWARE_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/Sensors/BLE.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/Sensors/EEPROM.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/Sensors/EEPROM_Utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/Sensors/MAX17043.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/Sensors/MS5611.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/Sensors/WiFi_AP.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/Sensors/SDCard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hardware/Sensors/SC7A22H.cpp
)

set(POWER_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Power/PowerManager.cpp
)

set(TASK_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Task/TaskManager.cpp
)

# 主组件注册
idf_component_register(
    SRCS 
    "main.cpp"
    "Initialize.cpp"
    "system_diagnostics.cpp"
    "common_func.cpp"
    ${ALGORITHM_SRCS}
    ${HARDWARE_SRCS}
    ${POWER_SRCS}
    ${TASK_SRCS}

    INCLUDE_DIRS 
    "." 
    "ulp"
    ${CMAKE_CURRENT_SOURCE_DIR}/include/AI
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Algorithm
    ${CMAKE_CURRENT_SOURCE_DIR}/include/HARDWARE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/HARDWARE/Sensors
    ${CMAKE_CURRENT_SOURCE_DIR}/include/HARDWARE/Display
    ${CMAKE_CURRENT_SOURCE_DIR}/include/POWER
    ${CMAKE_CURRENT_SOURCE_DIR}/include/TASK

    REQUIRES 
    ulp 
    soc
    bt
    app_update
    lwip
    nvs_flash
    esp_hw_support
    esp_mm
    esp-dsp
    fatfs
    sdmmc
    tinyusb

    MS5611 
    FastLED 
    SoftWire
    ArduinoEigen
    ESPAsyncWebServer
    esp-tflite-micro
    esp_websocket_client
    ICM20948
    sd_card
)

message(STATUS "ALGORITHM_INCLUDES = ${ALGORITHM_INCLUDES}")

set(ulp_riscv_include_dir "$ENV{IDF_PATH}/components/ulp/ulp_riscv/include")

# 构建ULP二进制并嵌入项目
ulp_embed_binary(${ulp_app_name}
    "${ulp_sources}" 
    "${ulp_exp_dep_srcs}"
    INCLUDE_PATHS "${ulp_riscv_include_dir}"
)

# 添加ulp构建脚本路径（如果需要自定义构建步骤）
set(ULP_BUILD_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/scripts/ulp_build.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ULP_BUILD_SCRIPT})