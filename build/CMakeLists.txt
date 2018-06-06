cmake_minimum_required(VERSION 2.8)

add_definitions("-pedantic -Werror -Wall -Wmissing-field-initializers")
set(CMAKE_C_FLAGS "-std=c90")

set(CMAKE_BUILD_TYPE "Release")

set(PROJECT_SOURCE_DIR ../src/)
set(PROJECT_TEST_DIR ../test/)

add_library(smalltalk ${PROJECT_SOURCE_DIR}smalltalk.c)

target_compile_options(smalltalk
  PRIVATE "-nostdlib"
  PRIVATE "-Os")

option(UNIT "run unit tests")
if(UNIT)
  function(unit_test name)
    add_executable("test-${name}" "${PROJECT_TEST_DIR}${name}.c")
    target_link_libraries("test-${name}" smalltalk)
    add_custom_command(TARGET "test-${name}"
      POST_BUILD
      COMMAND "./test-${name}")
  endfunction()

  unit_test(symbol)
  unit_test(class)
  unit_test(number)
  unit_test(array)
  unit_test(members)
endif(UNIT)

option(AUTOFORMAT "run clang-format after running make")
if(AUTOFORMAT)
  add_custom_command(TARGET smalltalk
    PRE_LINK
    COMMAND clang-format --style=file -i
    ${PROJECT_SOURCE_DIR}*.c ${PROJECT_SOURCE_DIR}*.h
    ${PROJECT_TEST_DIR}*.c)
endif(AUTOFORMAT)
