#include "fs/fat/FatOpenFile.hpp"

namespace fs
{
    bool FatOpenFile::Writable() const
    {
        return mode != OpenMode::read;
    }

    bool FatOpenFile::Uncommitted() const
    {
        return written || size != committedSize || firstCluster != committedFirstCluster;
    }

    void FatOpenFile::Committed()
    {
        committedSize = size;
        committedFirstCluster = firstCluster;
        written = false;
    }
}
