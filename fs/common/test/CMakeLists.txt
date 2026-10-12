add_executable(fs.common_test)
emil_build_for(fs.common_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.common_test)

target_link_libraries(fs.common_test PUBLIC
    gmock_main
    fs.common
)

target_sources(fs.common_test PRIVATE
    TestBlockDeviceResult.cpp
    TestPath.cpp
)
