# ── spymap static library ─────────────────────────────────────────────────────

add_library(spymap_lib STATIC
    ${CMAKE_SOURCE_DIR}/spymap/spymap.cpp
)

target_include_directories(spymap_lib PUBLIC
    ${CMAKE_SOURCE_DIR}
    /opt/homebrew/include/
)

set_target_properties(spymap_lib PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/spymap/lib"
    OUTPUT_NAME "spymap"
)

# ── spymap test executable ────────────────────────────────────────────────────

add_executable(spymap_test
    ${CMAKE_SOURCE_DIR}/spymap/test_mp.cpp
)

target_link_libraries(spymap_test PRIVATE spymap_lib)

set_target_properties(spymap_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/spymap"
    OUTPUT_NAME "spymap"
)

# ── spymap Python bindings (optional — requires pybind11) ─────────────────────
# Install with: brew install pybind11

find_package(pybind11 QUIET)

if(pybind11_FOUND)
    pybind11_add_module(spymap_python
        ${CMAKE_SOURCE_DIR}/spymap/python/spymap_python.cpp
    )
    target_link_libraries(spymap_python PRIVATE spymap_lib)
    target_include_directories(spymap_python PRIVATE
        ${CMAKE_SOURCE_DIR}
        /opt/homebrew/include/
    )
    set_target_properties(spymap_python PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/spymap/python"
        OUTPUT_NAME "spymap"
    )
    message(STATUS "pybind11 found — Python bindings enabled")
else()
    message(STATUS "pybind11 not found — skipping Python bindings (brew install pybind11)")
endif()
