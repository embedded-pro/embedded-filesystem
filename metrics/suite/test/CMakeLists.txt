add_executable(metrics.suite_test)
emil_build_for(metrics.suite_test BOOL EMFS_BUILD_METRICS)
emil_add_test(metrics.suite_test)

target_link_libraries(metrics.suite_test PUBLIC
    gmock_main
    metrics.suite
    metrics.workload_test_doubles
    infra.timer_test_helper
)

target_sources(metrics.suite_test PRIVATE
    TestMeasurement.cpp
    TestMeasurements.cpp
    TestMetricRecorder.cpp
    TestPhaseWrites.cpp
)
