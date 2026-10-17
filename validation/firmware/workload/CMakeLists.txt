add_library(validation.workload ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.workload PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
)

target_link_libraries(validation.workload PUBLIC
    fs.interfaces
    fs.nor
    infra.timer
    services.hil.commands
    validation.devices
    validation.hil
    validation.results
    validation.timing
)

target_sources(validation.workload PRIVATE
    AppendWindows.cpp
    AppendWindows.hpp
    CountRecords.cpp
    CountRecords.hpp
    DataLogger.cpp
    DataLogger.hpp
    FileWriter.cpp
    FileWriter.hpp
    LogAppender.cpp
    LogAppender.hpp
    MountScaling.cpp
    MountScaling.hpp
    NorWorkloadVolume.hpp
    PhaseTotals.cpp
    PhaseTotals.hpp
    RandomOverwrite.cpp
    RandomOverwrite.hpp
    ReadPatterns.cpp
    ReadPatterns.hpp
    SequentialWrite.cpp
    SequentialWrite.hpp
    SmallFiles.cpp
    SmallFiles.hpp
    VolumeFiller.cpp
    VolumeFiller.hpp
    Workload.cpp
    Workload.hpp
    WorkloadCommands.cpp
    WorkloadCommands.hpp
    WorkloadOperation.cpp
    WorkloadOperation.hpp
    WorkloadRunner.cpp
    WorkloadRunner.hpp
    WorkloadVolume.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
    add_subdirectory(test_doubles)
endif()
