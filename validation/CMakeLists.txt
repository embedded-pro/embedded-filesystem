if (TARGET_MCU_VENDOR STREQUAL st)
    include(FetchContent)

    FetchContent_Declare(
        halst
        GIT_REPOSITORY https://github.com/embedded-pro/hal-st.git
        GIT_TAG        75e9c188838c1c6b532b80ad3b160210c2e4887e # Unreleased
    )

    FetchContent_MakeAvailable(halst)
endif()

add_subdirectory(firmware)

if (NOT EMIL_HOST_BUILD AND NOT TARGET_MCU_VENDOR)
    add_subdirectory(footprint)
endif()

if (EMFS_BUILD_METRICS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(fragments "${CMAKE_CURRENT_BINARY_DIR}/fragments")

    add_test(NAME validation.convert_runs
        COMMAND Python3::Interpreter -m emfs_validation.convert
            --results "${CMAKE_CURRENT_SOURCE_DIR}/results"
            --output-directory "${fragments}"
    )
    set_tests_properties(validation.convert_runs PROPERTIES
        LABELS metrics
        ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/host"
    )
    emfs_add_metrics_fragments(validation.convert_runs "${fragments}")
endif()
