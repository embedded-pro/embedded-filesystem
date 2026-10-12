add_executable(metrics.fat_test)
emil_build_for(metrics.fat_test BOOL EMFS_BUILD_METRICS)
emil_add_test(metrics.fat_test)

target_link_libraries(metrics.fat_test PUBLIC
    gmock_main
    metrics.fat
    fs.fat_test_doubles
    hal.interfaces_test_doubles
    infra.event_test_helper
)

target_sources(metrics.fat_test PRIVATE
    TestFatChecker.cpp
    TestFatSweep.cpp
)
