cmake_minimum_required(VERSION 3.16)

file(GLOB_RECURSE AI_SRCS "*.c" "*.cpp")
set(AI_SRCS ${AI_SRCS} PARENT_SCOPE)
