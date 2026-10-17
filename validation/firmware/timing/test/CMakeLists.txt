add_executable(validation.timing_test)
emil_build_for(validation.timing_test BOOL EMFS_BUILD_TESTS)
emil_add_test(validation.timing_test)

target_link_libraries(validation.timing_test PUBLIC
    gmock_main
    validation.timing
)

target_sources(validation.timing_test PRIVATE
    TestSamples.cpp
)
