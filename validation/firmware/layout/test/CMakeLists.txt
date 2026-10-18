add_executable(validation.layout_test)
emil_build_for(validation.layout_test BOOL EMFS_BUILD_TESTS)
emil_add_test(validation.layout_test)

target_link_libraries(validation.layout_test PUBLIC
    gmock_main
    hal.interfaces_test_doubles
    infra.timer_test_helper
    infra.util_test_helper
    validation.devices_test_doubles
    validation.layout
)

target_sources(validation.layout_test PRIVATE
    TestFatLayout.cpp
    TestLayoutCommands.cpp
)
