include(GNUInstallDirs)

add_executable(kv_basic_test
    basic_test.cpp
)

target_include_directories(kv_basic_test
    PRIVATE "${CMAKE_SOURCE_DIR}/src/lib"
)

target_link_libraries(kv_basic_test
    key_value_store
    pthread
)

set_target_properties(kv_basic_test
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(kv_stress_test
    stress_test.cpp
)

target_include_directories(kv_stress_test
    PRIVATE "${CMAKE_SOURCE_DIR}/src/lib"
)

target_link_libraries(kv_stress_test
    key_value_store
    pthread
)

set_target_properties(kv_stress_test
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
