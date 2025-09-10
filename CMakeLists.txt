cmake_minimum_required(VERSION 3.22)
project(unix_socket_server CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(GTest REQUIRED)

add_subdirectory(non_blocking_unix_socket_server)
add_subdirectory(src)