add_executable(spike.footprint.fat_reader)
set_target_properties(spike.footprint.fat_reader PROPERTIES
    SUFFIX .elf
    LINK_DEPENDS "${PROJECT_SOURCE_DIR}/validation/footprint/Footprint.ld"
)
target_link_options(spike.footprint.fat_reader PRIVATE "-T${PROJECT_SOURCE_DIR}/validation/footprint/Footprint.ld" "LINKER:--undefined=Reset_Handler,--no-warn-rwx-segments" "LINKER:-Map=$<TARGET_FILE:spike.footprint.fat_reader>.map")
target_link_libraries(spike.footprint.fat_reader PRIVATE hal.cortex_m.runtime validation.footprint_platform validation.footprint_exercise spike.fat_reader)
target_sources(spike.footprint.fat_reader PRIVATE FatReaderMain.cpp)
