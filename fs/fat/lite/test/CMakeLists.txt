add_executable(fs.fat_lite_test)
emil_build_for(fs.fat_lite_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.fat_lite_test)

target_link_libraries(fs.fat_lite_test PUBLIC
    gmock_main
    fs.fat_lite
    fs.fat_test_doubles
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
)

target_sources(fs.fat_lite_test PRIVATE
    FatLiteFixture.hpp
    TestFatLite.cpp
    TestFatLiteFaults.cpp
    TestFatLiteFile.cpp
    TestFatLiteWindow.cpp
)
