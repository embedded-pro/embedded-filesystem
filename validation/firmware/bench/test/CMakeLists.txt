add_executable(validation.bench_test)
emil_build_for(validation.bench_test BOOL EMFS_BUILD_TESTS)
emil_add_test(validation.bench_test)

target_link_libraries(validation.bench_test PUBLIC
    gmock_main
    hal.interfaces_test_doubles
    infra.timer_test_helper
    infra.util_test_helper
    validation.bench
    validation.bench_test_doubles
    validation.timing_test_doubles
)

target_sources(validation.bench_test PRIVATE
    TestBenchCommands.cpp
    TestNorBench.cpp
)
