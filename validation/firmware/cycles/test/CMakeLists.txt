add_executable(validation.cycles_test)
emil_build_for(validation.cycles_test BOOL EMFS_BUILD_TESTS)
emil_add_test(validation.cycles_test)

target_link_libraries(validation.cycles_test PUBLIC
    gmock_main
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
    hal.interfaces_test_doubles
    validation.cycles
)

target_sources(validation.cycles_test PRIVATE
    TestCycleCounting.cpp
    TestCycleRun.cpp
)
