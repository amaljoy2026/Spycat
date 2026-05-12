cmake_minimum_required(VERSION 3.16)
project(spycat)

set(CMAKE_CXX_STANDARD 17)  # use C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(spymap/spymap.cmake)
include(spyscope/spyscope.cmake)