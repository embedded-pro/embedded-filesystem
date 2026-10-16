add_subdirectory(hil)
add_subdirectory(results)

if (TARGET_MCU_VENDOR STREQUAL st)
    add_subdirectory(boards)
endif()
