# SoftWire
cmake_minimum_required(VERSION 3.5)

# Register the component with ESP-IDF
idf_component_register(SRCS "src/SoftWire.cpp"
                       INCLUDE_DIRS "src"
                       REQUIRES arduino-esp32 AsyncDelay)

project(SoftWire)
