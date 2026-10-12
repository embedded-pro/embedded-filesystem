add_executable(fs.fat_test)
emil_build_for(fs.fat_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.fat_test)

target_link_libraries(fs.fat_test PUBLIC
    gmock_main
    fs.fat
)

target_sources(fs.fat_test PRIVATE
    TestFatBootSector.cpp
    TestFatFsInfo.cpp
    TestFatGeometry.cpp
    TestFatPartitionTable.cpp
    TestFatTable.cpp
)
