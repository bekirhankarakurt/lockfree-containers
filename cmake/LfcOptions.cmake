# Compiler settings applied to the project's own tests, benchmarks and
# examples. Consumers of the header-only target are not affected.

add_library(lfc_project_options INTERFACE)
target_compile_features(lfc_project_options INTERFACE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lfc_project_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual)
    if(LFC_WARNINGS_AS_ERRORS)
        target_compile_options(lfc_project_options INTERFACE -Werror)
    endif()

    if(LFC_SANITIZER STREQUAL "address")
        set(_lfc_san -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    elseif(LFC_SANITIZER STREQUAL "thread")
        set(_lfc_san -fsanitize=thread -fno-omit-frame-pointer)
    elseif(NOT LFC_SANITIZER STREQUAL "")
        message(FATAL_ERROR "LFC_SANITIZER must be 'address', 'thread' or empty")
    endif()
    if(_lfc_san)
        target_compile_options(lfc_project_options INTERFACE ${_lfc_san})
        target_link_options(lfc_project_options INTERFACE ${_lfc_san})
    endif()

    if(LFC_COVERAGE)
        # Atomic counters: the tests are multi-threaded, and racy counters
        # produce corrupt (negative) hit counts.
        target_compile_options(lfc_project_options INTERFACE --coverage -fprofile-update=atomic -O0 -g)
        target_link_options(lfc_project_options INTERFACE --coverage)
    endif()
elseif(MSVC)
    target_compile_options(lfc_project_options INTERFACE /W4 /permissive-)
    if(LFC_WARNINGS_AS_ERRORS)
        target_compile_options(lfc_project_options INTERFACE /WX)
    endif()
endif()
