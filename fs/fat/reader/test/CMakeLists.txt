add_executable(fs.fat_reader_test)
emil_build_for(fs.fat_reader_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.fat_reader_test)

target_link_libraries(fs.fat_reader_test PUBLIC
    gmock_main
    fs.fat_reader
    fs.fat_test_doubles
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
)

target_sources(fs.fat_reader_test PRIVATE
    FatReaderFixture.hpp
    TestFatReader.cpp
    TestFatReaderDirectory.cpp
    TestFatReaderFile.cpp
    TestFatReaderWindow.cpp
)
