cmake_minimum_required(VERSION 3.20)
project(tcp_proxy C)

set(CMAKE_C_STANDARD 99)

add_executable(tcp-proxy src/main.c)

include_directories(include)
