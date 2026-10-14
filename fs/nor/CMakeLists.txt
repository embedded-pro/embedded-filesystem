add_library(fs.nor ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.nor PUBLIC
    fs.common
    fs.interfaces
    hal.interfaces
    infra.event
    infra.timer
    infra.util
    services.fsm
)

target_sources(fs.nor PRIVATE
    NorBlockHeader.cpp
    NorBlockHeader.hpp
    NorBlockStates.cpp
    NorBlockStates.hpp
    NorCheckpoint.cpp
    NorCheckpoint.hpp
    NorCheckpointLoad.cpp
    NorCheckpointLoad.hpp
    NorConfig.hpp
    NorEraseCounts.cpp
    NorEraseCounts.hpp
    NorFileSystem.cpp
    NorFileSystem.hpp
    NorFormatter.cpp
    NorFormatter.hpp
    NorGeometry.cpp
    NorGeometry.hpp
    NorHeaderScan.cpp
    NorHeaderScan.hpp
    NorInodeRecord.cpp
    NorInodeRecord.hpp
    NorJournalScan.cpp
    NorJournalScan.hpp
    NorLayout.hpp
    NorLifecycle.hpp
    NorMount.cpp
    NorMount.hpp
    NorNode.cpp
    NorNode.hpp
    NorRecord.cpp
    NorRecord.hpp
    NorRecordReader.cpp
    NorRecordReader.hpp
    NorSuperblock.cpp
    NorSuperblock.hpp
    NorVolume.cpp
    NorVolume.hpp
    NorVolumeState.cpp
    NorVolumeState.hpp
)

add_subdirectory(test_doubles)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
