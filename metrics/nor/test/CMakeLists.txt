add_executable(metrics.nor_test)
emil_build_for(metrics.nor_test BOOL EMFS_BUILD_METRICS)
emil_add_test(metrics.nor_test)

target_link_libraries(metrics.nor_test PUBLIC
    gmock_main
    metrics.nor
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
)

target_sources(metrics.nor_test PRIVATE
    TestNorChecker.cpp
)
