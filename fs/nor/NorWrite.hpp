#ifndef FS_NOR_NOR_WRITE_HPP
#define FS_NOR_NOR_WRITE_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorOpenFile.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorWrite
    {
    public:
        NorWrite(NorVolume& volume, NorOpenFile& file, infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone);
        NorWrite(const NorWrite& other) = delete;
        NorWrite& operator=(const NorWrite& other) = delete;
        ~NorWrite();

        void Start();

    private:
        void Next();
        bool Step();
        bool AppendToPage();
        void AllocateTail(uint32_t index);
        void TailAllocated(std::optional<uint32_t> block, uint32_t index);
        void FlushTo(uint32_t to, uint32_t appended);
        uint32_t Append(uint32_t count);
        uint32_t BlockStart() const;
        uint32_t DeviceOffset() const;
        uint32_t NextBoundary() const;
        uint32_t LastBoundary() const;

    private:
        NorVolume& volume;
        NorOpenFile& file;
        infra::ConstByteRange data;
        infra::Function<void(Result result)> onDone;
    };
}

#endif
