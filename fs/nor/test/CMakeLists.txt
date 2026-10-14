add_executable(fs.nor_test)
emil_build_for(fs.nor_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.nor_test)

target_link_libraries(fs.nor_test PUBLIC
    gmock_main
    fs.nor
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.util_test_helper
)

target_sources(fs.nor_test PRIVATE
    TestNorBlockHeader.cpp
    TestNorCheckpoint.cpp
    TestNorConfig.cpp
    TestNorFormatter.cpp
    TestNorGeometry.cpp
    TestNorInodeRecord.cpp
    TestNorNode.cpp
    TestNorRecord.cpp
    TestNorSuperblock.cpp
)
