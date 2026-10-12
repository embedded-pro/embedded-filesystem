add_executable(metrics.workload_test)
emil_build_for(metrics.workload_test BOOL EMFS_BUILD_METRICS)
emil_add_test(metrics.workload_test)

target_link_libraries(metrics.workload_test PUBLIC
    gmock_main
    metrics.workload_test_doubles
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
)

target_sources(metrics.workload_test PRIVATE
    TestMetricComputations.cpp
    TestPattern.cpp
    TestRandom.cpp
    TestRunner.cpp
    TestWorkloads.cpp
)
