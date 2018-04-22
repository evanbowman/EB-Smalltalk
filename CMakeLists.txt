cmake_minimum_required(VERSION 2.8)

add_definitions("-pedantic -Werror -Wall -Wextra -O2")

add_library(smalltalk
  smalltalk.c)

add_executable(unittest
  unittest.c)

target_link_libraries(unittest
  smalltalk)
