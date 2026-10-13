add_executable(fs.fat_test)
emil_build_for(fs.fat_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.fat_test)

target_link_libraries(fs.fat_test PUBLIC
    gmock_main
    fs.fat
    fs.fat_test_doubles
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
)

target_sources(fs.fat_test PRIVATE
    TestFatBootSector.cpp
    FatVolumeFixture.hpp
    TestFatDirectoryEntry.cpp
    TestFatDirectoryReader.cpp
    TestFatEntryAssembler.cpp
    TestFatFileSystem.cpp
    TestFatFormatLayout.cpp
    TestFatFormatter.cpp
    TestFatFsInfo.cpp
    TestFatGeometry.cpp
    TestFatName.cpp
    TestFatPartitionTable.cpp
    TestFatShortName.cpp
    TestFatTable.cpp
    TestFatTableReader.cpp
    TestSparseBlockDevice.cpp
    TestUtf8.cpp
)
