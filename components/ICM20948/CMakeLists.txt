cmake_minimum_required(VERSION 3.5)

idf_component_register(SRCS "src/ICM_20948.cpp" "src/util/ICM_20948_C.c"
                       INCLUDE_DIRS "src" "src/util"
                       REQUIRES arduino-esp32)

project(ICM20948)
