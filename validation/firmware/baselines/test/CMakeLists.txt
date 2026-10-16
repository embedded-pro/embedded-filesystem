add_executable(validation.baselines_test)
emil_build_for(validation.baselines_test BOOL EMFS_BUILD_TESTS)
emil_add_test(validation.baselines_test)

target_link_libraries(validation.baselines_test PUBLIC
    gmock_main
    hal.interfaces_test_doubles
    infra.event_test_helper
    validation.fatfs_disk_default
    validation.littlefs_flash
)

target_sources(validation.baselines_test PRIVATE
    TestFatFsDisk.cpp
    TestLittleFsFlash.cpp
)
