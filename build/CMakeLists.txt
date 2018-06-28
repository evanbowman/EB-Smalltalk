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
  unit_test(gc)
endif(UNIT)

option(AUTOFORMAT "run clang-format after running make")
if(AUTOFORMAT)
  add_custom_command(TARGET smalltalk
    PRE_LINK
    COMMAND clang-format --style=file -i
    ${PROJECT_SOURCE_DIR}*.c ${PROJECT_SOURCE_DIR}*.h
    ${PROJECT_TEST_DIR}*.c)
endif(AUTOFORMAT)


set(CMAKE_CXX_FLAGS "-std=c++11")

option(BUILD_COMPILER "build the compiler utility")
if(BUILD_COMPILER)
  set(SCC_SRC
    ../util/scc.cpp
    ../util/SmalltalkBaseListener.cpp
    ../util/SmalltalkLexer.cpp
    ../util/SmalltalkListener.cpp
    ../util/SmalltalkParser.cpp)

  add_executable(scc ${SCC_SRC})
  target_link_libraries(scc antlr4-runtime)
  target_include_directories(scc PRIVATE "/usr/local/include/antlr4-runtime")
endif(BUILD_COMPILER)

add_executable(svm ../util/svm.cpp)
target_link_libraries(svm smalltalk)

add_executable(bcprinter ../util/bytecodePrinter.cpp)
target_link_libraries(bcprinter smalltalk)
