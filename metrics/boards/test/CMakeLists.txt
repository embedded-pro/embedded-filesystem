add_executable(metrics.boards_test)
emil_build_for(metrics.boards_test BOOL EMFS_BUILD_METRICS)
emil_add_test(metrics.boards_test)

target_link_libraries(metrics.boards_test PUBLIC
    gmock_main
    metrics.boards
)

target_sources(metrics.boards_test PRIVATE
    TestBoardDevices.cpp
)
