add_executable(fs.nor_test)
emil_build_for(fs.nor_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.nor_test)

target_link_libraries(fs.nor_test PUBLIC
    gmock_main
    fs.nor
    fs.nor_test_doubles
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
)

target_sources(fs.nor_test PRIVATE
    TestNorBTree.cpp
    TestNorBlockHeader.cpp
    TestNorBlockStates.cpp
    TestNorCheckpoint.cpp
    TestNorConfig.cpp
    TestNorDirectories.cpp
    TestNorDirectoryKey.cpp
    TestNorEraseCounts.cpp
    TestNorFileSystem.cpp
    TestNorFormatter.cpp
    TestNorGeometry.cpp
    TestNorImageBuilder.cpp
    NorMapsFixture.hpp
    TestNorMapFetch.cpp
    TestNorMount.cpp
    TestNorInodeRecord.cpp
    TestNorNode.cpp
    TestNorNodeCache.cpp
    TestNorNodeWriteBack.cpp
    TestNorRadixTree.cpp
    TestNorRecord.cpp
    TestNorSuperblock.cpp
)
