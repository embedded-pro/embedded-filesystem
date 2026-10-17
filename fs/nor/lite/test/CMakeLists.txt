add_executable(fs.nor_lite_test)
emil_build_for(fs.nor_lite_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.nor_lite_test)

target_link_libraries(fs.nor_lite_test PUBLIC
    gmock_main
    fs.nor_lite
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
)

target_sources(fs.nor_lite_test PRIVATE
    NorLiteFixture.hpp
    TestNorLite.cpp
)
