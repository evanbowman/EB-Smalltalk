cmake_minimum_required(VERSION 2.8)


add_definitions("-pedantic -Werror -Wall")
set(CMAKE_C_FLAGS "-std=c90")
set(CMAKE_CXX_FLAGS "-std=c++11")


add_library(smalltalk smalltalk.c)


# add_executable(svm svm.c)
# target_link_libraries(svm smalltalk)

# add_executable(timing timing.cpp)
# target_link_libraries(timing smalltalk)

# find_package(FLEX)
# FLEX_TARGET(lexer lexer.l lexer.c)
# add_executable(scc scc.cpp lexer.c)


# add_executable(stbc-printer bytecodePrinter.cpp)


add_custom_command(TARGET smalltalk
  PRE_LINK
  COMMAND clang-format --style=file -i *.c *.h)
