#include "fs/nor/NorVolumeState.hpp"

namespace fs
{
    NorVolumeState::NorVolumeState(infra::ByteRange blockStates, infra::ByteRange eraseCountStorage, infra::ByteRange nodeBlockStorage)
        : blocks(blockStates)
        , eraseCounts(eraseCountStorage)
        , nodeBlocks(nodeBlockStorage)
    {}
}
