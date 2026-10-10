add_executable(metrics.littlefs_test)
emil_build_for(metrics.littlefs_test BOOL EMFS_BUILD_METRICS)
emil_add_test(metrics.littlefs_test)

target_link_libraries(metrics.littlefs_test PUBLIC
    gmock_main
    metrics.littlefs
    hal.interfaces_test_doubles
    infra.timer_test_helper
)

target_sources(metrics.littlefs_test PRIVATE
    TestLittleFsSubject.cpp
)
