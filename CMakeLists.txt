cmake_minimum_required(VERSION 3.16)
project(rill CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(rill
  src/main.cpp
  src/common/source.cpp
  src/common/diag.cpp
  src/lex/token.cpp
  src/lex/lexer.cpp
  src/lex/layout.cpp)

target_include_directories(rill PRIVATE src)
target_compile_options(rill PRIVATE -Wall -Wextra -Wpedantic)