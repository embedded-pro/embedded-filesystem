add_executable(fs.fat_fiber_test)
emil_build_for(fs.fat_fiber_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.fat_fiber_test)

target_link_libraries(fs.fat_fiber_test PUBLIC
    gmock_main
    fs.fat_fiber
    fs.fat_test_doubles
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
)

target_sources(fs.fat_fiber_test PRIVATE
    TestFatFiberReader.cpp
)
