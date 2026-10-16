add_subdirectory(hil)

if (TARGET_MCU_VENDOR STREQUAL st)
    add_subdirectory(boards)
endif()
