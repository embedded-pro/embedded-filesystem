add_executable(validation.devices_test)
emil_build_for(validation.devices_test BOOL EMFS_BUILD_TESTS)
emil_add_test(validation.devices_test)

target_link_libraries(validation.devices_test PUBLIC
    gmock_main
    infra.event_test_helper
    infra.util_test_helper
    validation.devices
)

target_sources(validation.devices_test PRIVATE
    TestNullDevices.cpp
)
