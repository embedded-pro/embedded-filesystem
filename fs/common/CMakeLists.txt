add_library(fs.common ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.common PUBLIC
    fs.interfaces
    hal.interfaces
    infra.event
    infra.timer
    infra.util
)

target_sources(fs.common PRIVATE
    BlockDeviceResult.cpp
    BlockDeviceResult.hpp
    Crc32.cpp
    Crc32.hpp
    CrcRecord.cpp
    CrcRecord.hpp
    DeferredCompletions.cpp
    DeferredCompletions.hpp
    Deliver.hpp
    DirectTransfers.cpp
    DirectTransfers.hpp
    FlushBarrier.cpp
    FlushBarrier.hpp
    HandlePool.cpp
    HandlePool.hpp
    IdleTimer.cpp
    IdleTimer.hpp
    LiteLifecycle.cpp
    LiteLifecycle.hpp
    LittleEndian.cpp
    LittleEndian.hpp
    OperationQueue.cpp
    OperationQueue.hpp
    Path.cpp
    Path.hpp
    SectorCache.cpp
    SectorCache.hpp
    SectorSource.hpp
    SectorStore.hpp
    TransferAlignment.cpp
    TransferAlignment.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
