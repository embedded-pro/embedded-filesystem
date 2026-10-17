add_executable(fs.common_test)
emil_build_for(fs.common_test BOOL EMFS_BUILD_TESTS)
emil_add_test(fs.common_test)

target_link_libraries(fs.common_test PUBLIC
    gmock_main
    fs.common
    hal.interfaces_test_doubles
    infra.event_test_helper
    infra.timer_test_helper
    infra.util_test_helper
)

target_sources(fs.common_test PRIVATE
    TestBlockDeviceResult.cpp
    TestCrc32.cpp
    TestCrcRecord.cpp
    TestDeferredCompletions.cpp
    TestDirectTransfers.cpp
    TestFlushBarrier.cpp
    TestHandlePool.cpp
    TestIdleTimer.cpp
    TestLittleEndian.cpp
    TestOperationQueue.cpp
    TestPath.cpp
    TestSectorCache.cpp
    TestTransferAlignment.cpp
)
