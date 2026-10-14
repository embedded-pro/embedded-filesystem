#include "fs/nor/NorVolumeState.hpp"

namespace fs
{
    NorVolumeState::NorVolumeState(infra::ByteRange blockStates, infra::ByteRange eraseCountStorage)
        : blocks(blockStates)
        , eraseCounts(eraseCountStorage)
    {}
}
