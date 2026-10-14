#ifndef FS_NOR_NOR_READ_HPP
#define FS_NOR_NOR_READ_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorOpenFile.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    class NorVolume;

    class NorRead
    {
    public:
        NorRead(NorVolume& volume, NorOpenFile& file, infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone);
        NorRead(const NorRead& other) = delete;
        NorRead& operator=(const NorRead& other) = delete;
        ~NorRead();

        void Start();

    private:
        void RecordRead(Result result);
        void NextBlock();
        void Fetched(NorFetchResult result);
        void DataRead();
        void Done(Result result) const;
        bool IsDataBlock(uint32_t block) const;

    private:
        NorVolume& volume;
        NorOpenFile& file;
        infra::ByteRange buffer;
        NorMapRoot map;
        uint32_t current{ 0 };
        uint32_t dataEnd{ 0 };
        uint32_t readEnd{ 0 };
        uint32_t chunk{ 0 };
        infra::Function<void(Result result, std::size_t bytesRead)> onDone;
    };
}

#endif
