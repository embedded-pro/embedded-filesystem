add_executable(metrics.baselines_test)
emil_build_for(metrics.baselines_test BOOL EMFS_BUILD_METRICS)
emil_add_test(metrics.baselines_test)

target_link_libraries(metrics.baselines_test PUBLIC
    gmock_main
    metrics.baselines
    metrics.littlefs
    fs.nor
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
)

target_sources(metrics.baselines_test PRIVATE
    TestFootprintConfigurations.cpp
    TestSynchronousDevices.cpp
)
