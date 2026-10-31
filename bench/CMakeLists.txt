add_executable(lfc_bench bench_main.cpp)
target_link_libraries(lfc_bench PRIVATE lfc::lfc lfc_project_options)
