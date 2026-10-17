add_library(validation.cycles_subjects ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.cycles_subjects PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../../..>"
)

target_link_libraries(validation.cycles_subjects PUBLIC
    fs.fat
    fs.nor
    validation.cycles
    validation.fatfs_disk_tuned
    validation.littlefs_flash
)

target_sources(validation.cycles_subjects PRIVATE
    BlockingSubject.cpp
    BlockingSubject.hpp
    CycleConfigurations.hpp
    FatFsSubject.cpp
    FatFsSubject.hpp
    FileSystemSubject.cpp
    FileSystemSubject.hpp
    LittleFsSubject.cpp
    LittleFsSubject.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
