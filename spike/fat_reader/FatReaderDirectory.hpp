#ifndef SPIKE_FAT_READER_FAT_READER_DIRECTORY_HPP
#define SPIKE_FAT_READER_FAT_READER_DIRECTORY_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/interfaces/Directory.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/SharedPtr.hpp"
#include "spike/fat_reader/FatReaderVolume.hpp"
#include <cstdint>
#include <optional>

namespace spike
{
    class FatReaderDirectory
        : public fs::Directory
        , public fs::PooledHandle
    {
    public:
        explicit FatReaderDirectory(FatReaderVolume& volume);
        FatReaderDirectory(const FatReaderDirectory& other) = delete;
        FatReaderDirectory& operator=(const FatReaderDirectory& other) = delete;
        ~FatReaderDirectory() = default;

        void Open(uint32_t cluster);
        void Next(const infra::Function<void(fs::Result result, std::optional<fs::EntryInfo> entry)>& onDone) override;

    private:
        void UnReferenced() override;
        void Granted();
        void Scanned(fs::Result result, std::optional<FatEntry> entry);

    private:
        FatReaderVolume& volume;
        infra::ClaimableResource::Claimer claimer;
        uint32_t generation{ 0 };
        FatPosition position;
        infra::SharedPtr<FatReaderDirectory> self;
        infra::AutoResetFunction<void(fs::Result result, std::optional<fs::EntryInfo> entry)> onDone;
    };
}

#endif
