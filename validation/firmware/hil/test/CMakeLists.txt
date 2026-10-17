add_executable(validation.hil_test)
emil_build_for(validation.hil_test BOOL EMFS_BUILD_TESTS)
emil_add_test(validation.hil_test)

target_link_libraries(validation.hil_test PUBLIC
    gmock_main
    hal.interfaces_test_doubles
    infra.timer_test_helper
    validation.hil
)

target_sources(validation.hil_test PRIVATE
    TestDeviceCommands.cpp
    TestFirmwareCommands.cpp
    TestNoPins.cpp
)
