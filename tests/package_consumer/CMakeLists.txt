# Verifies that an installed lfc package can be consumed with find_package.
cmake_minimum_required(VERSION 3.16)
project(lfc_package_consumer LANGUAGES CXX)

find_package(lfc 1.0 CONFIG REQUIRED)

add_executable(package_consumer main.cpp)
target_link_libraries(package_consumer PRIVATE lfc::lfc)
