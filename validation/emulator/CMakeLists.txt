if (TARGET_CORTEX STREQUAL m4)
    set(EMULATOR_MACHINE mps2-an386)
    set(EMULATOR_CODE 0x00000000)
    set(EMULATOR_RAM 0x20000000)
elseif (TARGET_CORTEX STREQUAL m7)
    set(EMULATOR_MACHINE mps2-an500)
    set(EMULATOR_CODE 0x00000000)
    set(EMULATOR_RAM 0x20000000)
elseif (TARGET_CORTEX STREQUAL m33)
    set(EMULATOR_MACHINE mps2-an505)
    set(EMULATOR_CODE 0x10000000)
    set(EMULATOR_RAM 0x38000000)
else()
    return()
endif()

configure_file(Emulator.ld.in Emulator.ld @ONLY)

add_library(validation.emulator_platform ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.emulator_platform PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
)

target_link_libraries(validation.emulator_platform PUBLIC
    hal.cortex_m
    infra.event
)

target_sources(validation.emulator_platform PRIVATE
    Emulator.cpp
    Emulator.hpp
    Startup.cpp
)

add_custom_target(validation.emulator)

function(emfs_add_emulator_image image)
    cmake_parse_arguments(PARSE_ARGV 1 IMAGE "" "" "SOURCES;LIBRARIES")

    add_executable(validation.emulator.${image})
    set_target_properties(validation.emulator.${image} PROPERTIES
        SUFFIX .elf
        LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/Emulator.ld"
    )
    target_link_options(validation.emulator.${image} PRIVATE "-T${CMAKE_CURRENT_BINARY_DIR}/Emulator.ld" "LINKER:--undefined=Reset_Handler,--no-warn-rwx-segments")
    target_link_libraries(validation.emulator.${image} PRIVATE hal.cortex_m.runtime validation.emulator_platform ${IMAGE_LIBRARIES})
    target_sources(validation.emulator.${image} PRIVATE ${IMAGE_SOURCES})

    add_custom_target(validation.emulator.${image}.run
        COMMAND qemu-system-arm -M ${EMULATOR_MACHINE} -icount shift=0 -nographic -monitor none -serial none
            -semihosting-config enable=on,target=native -kernel "$<TARGET_FILE:validation.emulator.${image}>"
        DEPENDS validation.emulator.${image}
        USES_TERMINAL
    )
    add_dependencies(validation.emulator validation.emulator.${image}.run)
endfunction()

emfs_add_emulator_image(fiber SOURCES FiberMain.cpp LIBRARIES fs.common)
emfs_add_emulator_image(fat_reader SOURCES FatReaderMain.cpp LIBRARIES fs.fat fs.fat_reader validation.devices)
