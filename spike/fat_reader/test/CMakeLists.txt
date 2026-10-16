add_executable(spike.fat_reader_test)
emil_build_for(spike.fat_reader_test BOOL EMFS_BUILD_TESTS)
emil_add_test(spike.fat_reader_test)

target_link_libraries(spike.fat_reader_test PUBLIC
    gmock_main
    fs.fat_test_doubles
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
    spike.fat_reader
)

target_sources(spike.fat_reader_test PRIVATE
    TestFatReader.cpp
)
