add_executable(metrics.device_test)
emil_build_for(metrics.device_test BOOL EMFS_BUILD_METRICS)
emil_add_test(metrics.device_test)

target_link_libraries(metrics.device_test PUBLIC
    gmock_main
    metrics.device
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
)

target_sources(metrics.device_test PRIVATE
    TestBlockDeviceStatistics.cpp
    TestDeviceModels.cpp
    TestFlashStatistics.cpp
    TestMeteredBlockDevice.cpp
    TestMeteredFlash.cpp
    TestVirtualTime.cpp
    TestVirtualTimeEnvironment.cpp
)
