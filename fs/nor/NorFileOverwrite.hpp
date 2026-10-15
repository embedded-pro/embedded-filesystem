#ifndef FS_NOR_NOR_FILE_OVERWRITE_HPP
#define FS_NOR_NOR_FILE_OVERWRITE_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorOpenFile.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorFileOverwrite
    {
    public:
        explicit NorFileOverwrite(NorVolume& volume);
        NorFileOverwrite(const NorFileOverwrite& other) = delete;
        NorFileOverwrite& operator=(const NorFileOverwrite& other) = delete;
        ~NorFileOverwrite();

        void Overwrite(NorOpenFile& file, infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone);

    private:
        void Next();
        bool Step();
        void CopyBlock();
        void FindSource();
        void SourceFetched(NorFetchResult result);
        void SourceFound(uint32_t block, bool uncommitted);
        void Allocated(std::optional<uint32_t> block);
        void CopyNextChunk();
        void Merge(infra::ByteRange chunkBytes) const;
        void BlockCopied();
        void Buffer(uint32_t to);
        void Advance(uint32_t to);
        uint32_t BlockStart() const;
        uint32_t SegmentEnd() const;
        bool IsDataBlock(uint32_t block) const;
        NorMapId InodeTable() const;
        NorMapId DataMap() const;

    private:
        NorVolume& volume;
        NorOpenFile* file{ nullptr };
        infra::ConstByteRange data;
        uint32_t index{ 0 };
        uint32_t source{ 0 };
        bool sourceUncommitted{ false };
        uint32_t copy{ 0 };
        uint32_t length{ 0 };
        uint32_t copied{ 0 };
        uint32_t chunk{ 0 };
        NorMapRoot dataMap;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
