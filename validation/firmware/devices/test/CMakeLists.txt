add_executable(validation.devices_test)
emil_build_for(validation.devices_test BOOL EMFS_BUILD_TESTS)
emil_add_test(validation.devices_test)

target_link_libraries(validation.devices_test PUBLIC
    gmock_main
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
    validation.devices
    validation.timing_test_doubles
)

target_sources(validation.devices_test PRIVATE
    TestCountingFlash.cpp
    TestNullDevices.cpp
    TestRamDevices.cpp
)
