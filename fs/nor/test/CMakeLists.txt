add_executable(fs.nor_test)
emil_build_for(fs.nor_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.nor_test)

target_link_libraries(fs.nor_test PUBLIC
    gmock_main
    fs.nor
    infra.util_test_helper
)

target_sources(fs.nor_test PRIVATE
    TestNorBlockHeader.cpp
    TestNorConfig.cpp
    TestNorGeometry.cpp
    TestNorNode.cpp
    TestNorRecord.cpp
    TestNorSuperblock.cpp
)
