if (TARGET_MCU_VENDOR STREQUAL st)
    include(FetchContent)

    FetchContent_Declare(
        halst
        GIT_REPOSITORY https://github.com/embedded-pro/hal-st.git
        GIT_TAG        4a22346ad03cc50f020e4f7adbb0e053667f7a57 # Unreleased
    )

    FetchContent_MakeAvailable(halst)
endif()

add_subdirectory(firmware)

if (NOT EMIL_HOST_BUILD AND NOT TARGET_MCU_VENDOR)
    add_subdirectory(footprint)
    add_subdirectory(emulator)
endif()

if (EMFS_BUILD_METRICS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(fragments "${CMAKE_CURRENT_BINARY_DIR}/fragments")
    set(models "${CMAKE_CURRENT_BINARY_DIR}/device_models.json")

    add_test(NAME validation.device_models
        COMMAND metrics.device_models "${models}"
    )
    set_tests_properties(validation.device_models PROPERTIES
        LABELS metrics
        FIXTURES_SETUP validation.device_models
    )

    add_test(NAME validation.convert_runs
        COMMAND Python3::Interpreter -m emfs_validation.convert
            --results "${CMAKE_CURRENT_SOURCE_DIR}/results"
            --models "${models}"
            --output-directory "${fragments}"
    )
    set_tests_properties(validation.convert_runs PROPERTIES
        LABELS metrics
        ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/host"
        FIXTURES_REQUIRED validation.device_models
    )
    emfs_add_metrics_fragments(validation.convert_runs "${fragments}")
endif()
