cmake_minimum_required(VERSION 3.1)

###########
# Project #
###########

project(key_value_store)

set(CMAKE_CXX_STANDARD 14)

add_subdirectory("src/lib")
add_subdirectory("src/tester")
