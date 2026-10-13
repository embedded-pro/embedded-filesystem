#ifndef FS_FAT_FAT_EXPECT_SIZE_HPP
#define FS_FAT_FAT_EXPECT_SIZE_HPP

#include "fs/fat/FatExtentCache.hpp"
#include "fs/fat/FatOpenFile.hpp"
#include "fs/fat/FatRunSearch.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatVolume;

    class FatExpectSize
    {
    public:
        FatExpectSize(FatVolume& volume, FatOpenFile& file, FatExtentCache& extents, uint32_t size, const infra::Function<void(Result result)>& onDone);
        FatExpectSize(const FatExpectSize& other) = delete;
        FatExpectSize& operator=(const FatExpectSize& other) = delete;
        ~FatExpectSize();

        void Start();

    private:
        void Search(std::optional<uint32_t> after);

    private:
        FatVolume& volume;
        FatOpenFile& file;
        FatExtentCache& extents;
        uint32_t size;
        infra::Function<void(Result result)> onDone;
        FatRunSearch search;
        uint32_t wanted{ 0 };
    };
}

#endif
