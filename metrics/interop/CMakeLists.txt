add_executable(metrics.interop_test)
emil_build_for(metrics.interop_test BOOL EMFS_BUILD_METRICS)
emil_add_test(metrics.interop_test)

if (TEST metrics.interop_test)
    set_tests_properties(metrics.interop_test PROPERTIES LABELS interop)
endif()

target_link_libraries(metrics.interop_test PUBLIC
    gmock_main
    fs.fat
    metrics.fat
    metrics.device
    infra.event_test_helper
    infra.timer_test_helper
)

target_sources(metrics.interop_test PRIVATE
    InteropTools.cpp
    InteropTools.hpp
    TestFormatterInterop.cpp
    TestListingInterop.cpp
    TestMountInterop.cpp
    TestReadInterop.cpp
    TestWriteInterop.cpp
)
