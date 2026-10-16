add_executable(validation.results_test)
emil_build_for(validation.results_test BOOL EMFS_BUILD_TESTS)
emil_add_test(validation.results_test)

target_link_libraries(validation.results_test PUBLIC
    gmock_main
    hal.interfaces_test_doubles
    infra.timer_test_helper
    validation.results
)

target_sources(validation.results_test PRIVATE
    TestResultLines.cpp
)
