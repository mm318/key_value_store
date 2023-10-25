include(GNUInstallDirs)

add_executable(key_value_store_tester
    main.cpp
)

target_include_directories(key_value_store_tester
    PRIVATE "${CMAKE_SOURCE_DIR}/src/lib"
)

target_link_libraries(key_value_store_tester
    key_value_store
)

set_target_properties(key_value_store_tester
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
