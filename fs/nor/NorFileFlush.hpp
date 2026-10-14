#ifndef FS_NOR_NOR_FILE_FLUSH_HPP
#define FS_NOR_NOR_FILE_FLUSH_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorOpenFile.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorFileFlush
    {
    public:
        explicit NorFileFlush(NorVolume& volume);
        NorFileFlush(const NorFileFlush& other) = delete;
        NorFileFlush& operator=(const NorFileFlush& other) = delete;
        ~NorFileFlush();

        void Flush(NorOpenFile& file, uint32_t to, const infra::Function<void(Result result)>& onDone);

    private:
        void Copied(std::optional<uint32_t> block);
        void CopyNextChunk();
        void Program();
        uint32_t Index() const;
        uint32_t OffsetInBlock(uint32_t offset) const;

    private:
        NorVolume& volume;
        NorOpenFile* file{ nullptr };
        uint32_t to{ 0 };
        uint32_t copy{ 0 };
        uint32_t copied{ 0 };
        uint32_t chunk{ 0 };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
