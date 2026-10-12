add_executable(metrics.report_test)
emil_build_for(metrics.report_test BOOL EMFS_BUILD_METRICS)
emil_add_test(metrics.report_test)

target_link_libraries(metrics.report_test PUBLIC
    gmock_main
    metrics.report
)

target_sources(metrics.report_test PRIVATE
    TestReport.cpp
    TestReportEnvironment.cpp
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME metrics.report_merge_test
    COMMAND Python3::Interpreter -m unittest discover --start-directory ${CMAKE_CURRENT_LIST_DIR} --pattern "test_*.py"
)
