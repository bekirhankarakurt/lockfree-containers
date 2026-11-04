set(LFC_TESTS
    spsc_ring
    mpmc_queue
    lockfree_stack
    seqlock
    triple_buffer
    linearizability)

foreach(name IN LISTS LFC_TESTS)
    add_executable(test_${name} test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE lfc::lfc lfc_project_options)
    add_test(NAME ${name} COMMAND test_${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 300)
endforeach()

if(LFC_BUILD_BENCHMARKS)
    add_test(NAME benchmark_smoke COMMAND lfc_bench --quick)
    set_tests_properties(benchmark_smoke PROPERTIES TIMEOUT 300)
endif()
