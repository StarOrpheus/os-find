cmake_minimum_required(VERSION 3.8)

project(findutil)

set(CMAKE_CXX_STANDARD 17)

add_executable(findutil main.cpp util.h util.cpp)
add_executable(findutil_asan main.cpp util.h util.cpp)

add_compile_options(findutil_asan PUBLIC -fsanitize=address)
add_link_options(findutil_asan -lasan)
