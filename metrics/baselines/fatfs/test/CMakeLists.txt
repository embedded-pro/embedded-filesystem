foreach(variant default tuned)
    add_executable(metrics.fatfs_${variant}_test)
    emil_build_for(metrics.fatfs_${variant}_test BOOL EMFS_BUILD_METRICS)
    emil_add_test(metrics.fatfs_${variant}_test)

    target_link_libraries(metrics.fatfs_${variant}_test PUBLIC
        gmock_main
        metrics.fatfs_${variant}
        hal.interfaces_test_doubles
        infra.timer_test_helper
    )

    target_sources(metrics.fatfs_${variant}_test PRIVATE
        TestFatFsSubject.cpp
    )
endforeach()
