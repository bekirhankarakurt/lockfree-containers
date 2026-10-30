cmake_minimum_required(VERSION 3.16)

project(lfc
    VERSION 1.0.0
    DESCRIPTION "Header-only lock-free containers for C++17"
    HOMEPAGE_URL "https://github.com/bekirhankarakurt/lockfree-containers"
    LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(LFC_IS_TOP_LEVEL ON)
else()
    set(LFC_IS_TOP_LEVEL OFF)
endif()

option(LFC_BUILD_TESTS "Build unit and stress tests" ${LFC_IS_TOP_LEVEL})
option(LFC_BUILD_BENCHMARKS "Build the benchmark harness" ${LFC_IS_TOP_LEVEL})
option(LFC_BUILD_EXAMPLES "Build examples" ${LFC_IS_TOP_LEVEL})
option(LFC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(LFC_COVERAGE "Instrument tests for coverage (GCC/Clang)" OFF)
option(LFC_INSTALL "Generate install and package config targets" ${LFC_IS_TOP_LEVEL})
set(LFC_SANITIZER "" CACHE STRING "Sanitizer for tests/benchmarks: address, thread, or empty")
set_property(CACHE LFC_SANITIZER PROPERTY STRINGS "" address thread)

include(GNUInstallDirs)

# ----------------------------------------------------------------- library
add_library(lfc INTERFACE)
add_library(lfc::lfc ALIAS lfc)
target_compile_features(lfc INTERFACE cxx_std_17)
target_include_directories(lfc INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
find_package(Threads REQUIRED)
target_link_libraries(lfc INTERFACE Threads::Threads)

# ------------------------------------------------- shared build settings
include(cmake/LfcOptions.cmake)

if(LFC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(LFC_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
if(LFC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ----------------------------------------------------------------- install
if(LFC_INSTALL)
    include(CMakePackageConfigHelpers)

    install(DIRECTORY include/lfc DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(TARGETS lfc EXPORT lfcTargets)
    install(EXPORT lfcTargets
        NAMESPACE lfc::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lfc)

    configure_package_config_file(cmake/lfcConfig.cmake.in
        ${PROJECT_BINARY_DIR}/lfcConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lfc)
    # Header-only: the package is architecture independent.
    write_basic_package_version_file(${PROJECT_BINARY_DIR}/lfcConfigVersion.cmake
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT)
    install(FILES
        ${PROJECT_BINARY_DIR}/lfcConfig.cmake
        ${PROJECT_BINARY_DIR}/lfcConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lfc)
endif()
