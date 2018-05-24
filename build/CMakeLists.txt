cmake_minimum_required(VERSION 2.8)

add_definitions("-pedantic -Werror -Wall")
set(CMAKE_C_FLAGS "-std=c90")
set(CMAKE_CXX_FLAGS "-std=c++11")

set(PROJECT_SOURCE_DIR ../src/)

add_library(smalltalk ${PROJECT_SOURCE_DIR}smalltalk.c)

add_custom_command(TARGET smalltalk
  PRE_LINK
  COMMAND clang-format --style=file -i
  ${PROJECT_SOURCE_DIR}*.c ${PROJECT_SOURCE_DIR}*.h)
