add_library(metrics.suite STATIC)

target_link_libraries(metrics.suite PUBLIC
    metrics.report
    metrics.workload
    hal.interfaces_test_doubles
)

target_sources(metrics.suite PRIVATE
    Measurement.cpp
    Measurement.hpp
    Measurements.cpp
    Measurements.hpp
    MetricRecorder.cpp
    MetricRecorder.hpp
    PhaseWrites.cpp
    PhaseWrites.hpp
)

# The parameterized metric tests register themselves on static initialization, so they are linked as objects
add_library(metrics.suite_tests OBJECT)

target_link_libraries(metrics.suite_tests PUBLIC
    metrics.suite
)

target_sources(metrics.suite_tests PRIVATE
    BlockMetrics.cpp
    BlockMetrics.hpp
    NorMetrics.cpp
    NorMetrics.hpp
)

set(EMFS_METRICS_FRAGMENT_DIRECTORY "${CMAKE_BINARY_DIR}/metrics/fragments" CACHE INTERNAL "")
set(EMFS_METRICS_BASELINE "" CACHE FILEPATH "metrics.json of main, to detect regressions of the reported metrics")
set(EMFS_METRICS_EXTRA_FRAGMENTS "" CACHE STRING "Absolute directories of report fragments made outside the build, such as the F-1 fragments of the metrics workflow")

add_test(NAME metrics.clean_fragments
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${EMFS_METRICS_FRAGMENT_DIRECTORY}"
)
set_tests_properties(metrics.clean_fragments PROPERTIES LABELS metrics)

# Every executable runs as several shards, so that ctest can run the slow metrics in parallel
function(emfs_add_metrics target)
    cmake_parse_arguments(PARSE_ARGV 1 METRICS "" "SHARDS" "SOURCES;LIBRARIES")
    if (NOT METRICS_SHARDS)
        set(METRICS_SHARDS 1)
    endif()

    add_executable(${target})
    emil_build_for(${target} BOOL EMFS_BUILD_METRICS)
    target_link_libraries(${target} PRIVATE metrics.suite_tests metrics.report_main ${METRICS_LIBRARIES})
    target_sources(${target} PRIVATE ${METRICS_SOURCES})

    math(EXPR last_shard "${METRICS_SHARDS} - 1")
    foreach(shard RANGE ${last_shard})
        set(test ${target}.${shard})
        add_test(NAME ${test}
            COMMAND ${target} "--metrics-output=${EMFS_METRICS_FRAGMENT_DIRECTORY}/${test}.json"
        )
        set_tests_properties(${test} PROPERTIES
            LABELS metrics
            DEPENDS metrics.clean_fragments
            ENVIRONMENT "GTEST_TOTAL_SHARDS=${METRICS_SHARDS};GTEST_SHARD_INDEX=${shard}"
        )
        set_property(GLOBAL APPEND PROPERTY EMFS_METRICS_TESTS ${test})
    endforeach()
endfunction()

function(emfs_add_metrics_fragments test directory)
    set_property(GLOBAL APPEND PROPERTY EMFS_METRICS_TESTS ${test})
    set_property(GLOBAL APPEND PROPERTY EMFS_METRICS_FRAGMENT_DIRECTORIES "${directory}")
endfunction()

function(emfs_add_metrics_merge)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_property(metric_tests GLOBAL PROPERTY EMFS_METRICS_TESTS)
    get_property(fragment_directories GLOBAL PROPERTY EMFS_METRICS_FRAGMENT_DIRECTORIES)

    if (EMFS_METRICS_BASELINE)
        set(baseline_argument "--baseline=${EMFS_METRICS_BASELINE}")
    endif()

    set(directory_arguments --fragment-directory "${EMFS_METRICS_FRAGMENT_DIRECTORY}")
    foreach(directory IN LISTS fragment_directories EMFS_METRICS_EXTRA_FRAGMENTS)
        list(APPEND directory_arguments --fragment-directory "${directory}")
    endforeach()

    add_test(NAME metrics.merge
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../report/merge_report.py"
            ${directory_arguments}
            --json "${CMAKE_BINARY_DIR}/metrics/metrics.json"
            --markdown "${CMAKE_BINARY_DIR}/metrics/metrics.md"
            ${baseline_argument}
            --check
    )
    set_tests_properties(metrics.merge PROPERTIES
        LABELS metrics
        DEPENDS "${metric_tests}"
    )
endfunction()

add_subdirectory(test)
