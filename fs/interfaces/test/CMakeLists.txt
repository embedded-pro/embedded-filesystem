add_executable(fs.interfaces_test)
emil_build_for(fs.interfaces_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.interfaces_test)

target_link_libraries(fs.interfaces_test PUBLIC
    gmock_main
    fs.interfaces
)

target_sources(fs.interfaces_test PRIVATE
    TestResult.cpp
)
