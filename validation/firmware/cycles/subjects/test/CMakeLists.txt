add_executable(validation.cycles_subjects_test)
emil_build_for(validation.cycles_subjects_test BOOL EMFS_BUILD_TESTS)
emil_add_test(validation.cycles_subjects_test)

target_link_libraries(validation.cycles_subjects_test PUBLIC
    gmock_main
    hal.interfaces_test_doubles
    infra.timer_test_helper
    infra.util_test_helper
    validation.cycles_subjects
    validation.devices
)

target_sources(validation.cycles_subjects_test PRIVATE
    TestCycleSubjects.cpp
)
