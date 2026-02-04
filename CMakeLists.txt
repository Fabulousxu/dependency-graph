cmake_minimum_required(VERSION 3.18)
project(depgraph LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include_directories(include thirdparty)
add_subdirectory(src)
add_subdirectory(benchmarks)
add_subdirectory(tests)