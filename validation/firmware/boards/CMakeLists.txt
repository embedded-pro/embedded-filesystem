if (TARGET_CORTEX STREQUAL m7)
    # Configuring again, as every build of the bench guide does, refreshes the commit that firmware.info reports
    execute_process(
        COMMAND git describe --always --dirty --abbrev=40
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        OUTPUT_VARIABLE commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE result
        ERROR_QUIET
    )
    if (NOT result EQUAL 0)
        set(commit unknown)
    endif()

    add_executable(validation.firmware)

    target_compile_definitions(validation.firmware PRIVATE
        EMFS_VALIDATION_COMMIT="${commit}"
    )

    target_include_directories(validation.firmware PRIVATE
        ${TARGET_MCU}
    )

    target_link_libraries(validation.firmware PRIVATE
        hal.cortex_m
        hal_st.instantiations
        hal_st.stm32fxxx
        services.hil
        services.tracer
        validation.bench
        validation.cycles
        validation.cycles_subjects
        validation.devices
        validation.hil
        validation.results
    )

    target_sources(validation.firmware PRIVATE
        ${TARGET_MCU}/BoardProfile.hpp
        BoardInfoStm.cpp
        BoardInfoStm.hpp
        Clocks.cpp
        Clocks.hpp
        EventInfrastructure.cpp
        EventInfrastructure.hpp
        MainCm7.cpp
        Memories.cpp
        Memories.hpp
    )

    halst_target_default_linker_scripts(validation.firmware)
    halst_target_bringup(validation.firmware)
    emil_generate_artifacts(TARGET validation.firmware LST MAP BIN HEX)
elseif (TARGET_CORTEX STREQUAL m4)
    add_executable(validation.park)

    target_include_directories(validation.park PRIVATE
        ${TARGET_MCU}
        ${CMAKE_CURRENT_LIST_DIR}/../../..
    )

    target_link_libraries(validation.park PRIVATE
        hal_st.stm32fxxx
    )

    target_sources(validation.park PRIVATE
        ${TARGET_MCU}/BoardProfile.hpp
        MainCm4.cpp
    )

    halst_target_default_linker_scripts(validation.park)
    halst_target_bringup(validation.park)
    emil_generate_artifacts(TARGET validation.park LST MAP BIN HEX)
endif()
