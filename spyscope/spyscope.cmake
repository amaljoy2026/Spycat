cmake_minimum_required(VERSION 3.16)
project(spycat)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(spyscope
    ${CMAKE_SOURCE_DIR}/spyscope/app.cpp
    ${CMAKE_SOURCE_DIR}/spyscope/spyplot.cpp
    ${CMAKE_SOURCE_DIR}/spyscope/spywatch.cpp
    ${CMAKE_SOURCE_DIR}/spyscope/spynav.cpp
    ${CMAKE_SOURCE_DIR}/spyscope/datasource.cpp
    ${CMAKE_SOURCE_DIR}/spyscope/dockpanel.cpp
    ${CMAKE_SOURCE_DIR}/spyscope/spydefault.cpp
    ${CMAKE_SOURCE_DIR}/spyscope/spysettings.cpp
)

target_include_directories(spyscope PRIVATE
    /opt/homebrew/include/
)

find_package(wxWidgets REQUIRED COMPONENTS core base aui)
include(${wxWidgets_USE_FILE})

target_link_libraries(spyscope PRIVATE
    spymap_lib
    ${wxWidgets_LIBRARIES}
)

set_target_properties(spyscope PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/spyscope"
)
