cmake_minimum_required(VERSION 3.16)
project(spycat)

set(CMAKE_CXX_STANDARD 20)  # use C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


add_executable(test_spymap
    src/spymap.cpp
    src/main.cpp
)

target_include_directories(test_spymap PRIVATE "/opt/homebrew/include/")