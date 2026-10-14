add_library(fs.nor ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.nor PUBLIC
    fs.common
    fs.interfaces
    hal.interfaces
    infra.util
)

target_sources(fs.nor PRIVATE
    NorBlockHeader.cpp
    NorBlockHeader.hpp
    NorConfig.hpp
    NorGeometry.cpp
    NorGeometry.hpp
    NorLayout.hpp
    NorNode.cpp
    NorNode.hpp
    NorRecord.cpp
    NorRecord.hpp
    NorSuperblock.cpp
    NorSuperblock.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
