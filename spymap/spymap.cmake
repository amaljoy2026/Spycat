add_executable(spymap
    ${CMAKE_SOURCE_DIR}/spymap/spymap.cpp
    ${CMAKE_SOURCE_DIR}/spymap/test_mp.cpp
)

target_include_directories(spymap PRIVATE 
    /opt/homebrew/include/
)

set_target_properties(spymap PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/spymap"
)

set_target_properties(spymap PROPERTIES 
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/spymap/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/spymap/lib"
)
