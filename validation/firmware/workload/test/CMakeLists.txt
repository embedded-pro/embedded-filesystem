add_executable(validation.workload_test)
emil_build_for(validation.workload_test BOOL EMFS_BUILD_TESTS)
emil_add_test(validation.workload_test)

target_link_libraries(validation.workload_test PUBLIC
    fs.interfaces_test_doubles
    gmock_main
    hal.interfaces_test_doubles
    infra.timer_test_helper
    infra.util_test_helper
    validation.timing_test_doubles
    validation.workload
    validation.workload_test_doubles
)

target_sources(validation.workload_test PRIVATE
    TestAppendWindows.cpp
    TestPhaseTotals.cpp
    TestWorkloadRunner.cpp
)
