cmake_minimum_required(VERSION 2.8)

add_definitions("-pedantic -Werror -Wall")

add_library(smalltalk smalltalk.c)

add_executable(unittest unittest.c)
target_link_libraries(unittest smalltalk)

add_executable(svm svm.c)
target_link_libraries(svm smalltalk)

set(CMAKE_CXX_FLAGS "-std=c++11")

add_executable(scc scc.cpp)

add_custom_command(TARGET smalltalk
  PRE_LINK
  COMMAND clang-format --style=file -i *.c *.h)
