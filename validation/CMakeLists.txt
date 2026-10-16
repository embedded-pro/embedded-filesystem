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
