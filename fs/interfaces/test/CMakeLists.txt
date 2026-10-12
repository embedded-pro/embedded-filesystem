add_executable(fs.interfaces_test)
emil_build_for(fs.interfaces_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.interfaces_test)

target_link_libraries(fs.interfaces_test PUBLIC
    gmock_main
    fs.interfaces
    fs.interfaces_test_doubles
    infra.util_test_helper
)

target_sources(fs.interfaces_test PRIVATE
    TestInterfaces.cpp
    TestResult.cpp
)
