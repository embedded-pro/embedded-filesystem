add_executable(metrics.power_cut_test)
emil_build_for(metrics.power_cut_test BOOL EMFS_BUILD_METRICS)
emil_add_test(metrics.power_cut_test)

target_link_libraries(metrics.power_cut_test PUBLIC
    gmock_main
    metrics.power_cut
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
)

target_sources(metrics.power_cut_test PRIVATE
    TestPowerCutBlockDevice.cpp
    TestPowerCutFlash.cpp
    TestPowerCutSweep.cpp
)
