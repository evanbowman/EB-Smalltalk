cmake_minimum_required(VERSION 2.8)

add_definitions("-pedantic -Werror -Wall -Wmissing-field-initializers")
set(CMAKE_C_FLAGS "-std=c90")
set(CMAKE_CXX_FLAGS "-std=c++11")

set(PROJECT_SOURCE_DIR ../src/)
set(PROJECT_TEST_DIR ../test/)

add_library(smalltalk ${PROJECT_SOURCE_DIR}smalltalk.c)

function(unit_test name)
  add_executable("test-${name}" "${PROJECT_TEST_DIR}${name}.c")
  target_link_libraries("test-${name}" smalltalk)
  add_custom_command(TARGET "test-${name}"
    POST_BUILD
    COMMAND "./test-${name}")
endfunction()

unit_test(symbol)
unit_test(class)

add_custom_command(TARGET smalltalk
  PRE_LINK
  COMMAND clang-format --style=file -i
  ${PROJECT_SOURCE_DIR}*.c ${PROJECT_SOURCE_DIR}*.h
  ${PROJECT_TEST_DIR}*.c)
