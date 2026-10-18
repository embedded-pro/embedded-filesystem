#ifndef FS_FAT_FAT_PARENT_DIRECTORY_HPP
#define FS_FAT_FAT_PARENT_DIRECTORY_HPP

#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatEntrySetWriter.hpp"
#include "fs/fat/FatNameScan.hpp"
#include "fs/fat/FatShortName.hpp"
#include "fs/fat/FatShortNameGeneration.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatVolume;

    class FatParentDirectory
    {
    public:
        explicit FatParentDirectory(FatVolume& volume);
        FatParentDirectory(const FatParentDirectory& other) = delete;
        FatParentDirectory& operator=(const FatParentDirectory& other) = delete;
        ~FatParentDirectory();

        void Find(infra::BoundedConstString path, bool forCreation, const infra::Function<void(Result result)>& onDone);
        void FindTarget(infra::BoundedConstString path, const FatScannedEntry& source, const infra::Function<void(Result result)>& onDone);
        const std::optional<FatScannedEntry>& Found() const;
        uint32_t Cluster() const;
        void PrepareAlias(const infra::Function<void(Result result)>& onDone);
        void Write(uint8_t attributes, uint32_t firstCluster, const infra::Function<void(Result result)>& onDone);
        void WriteEntry(ShortEntry entry, const infra::Function<void(Result result)>& onDone);
        FatScannedEntry Written() const;

    private:
        enum class Step : uint8_t
        {
            parentFound,
            aliasScanned,
            done
        };

        void Locate(infra::BoundedConstString path, uint32_t outside, const infra::Function<void(Result result)>& onDone);
        void OnParent(Result result);
        Result VerifyParent(Result result) const;
        void OnAliasScanned(Result result);
        void Prepared(const ShortName& prepared);
        const infra::Function<void(Result result)>& Then(Step next);
        void Resume(Result result);

    private:
        FatVolume& volume;
        infra::BoundedConstString name;
        bool forCreation{ false };
        Step step{ Step::parentFound };
        std::optional<FatEntryPosition> except;
        uint32_t cluster{ 0 };
        std::optional<ShortName> exact;
        ShortNameBytes alias{};
        ShortName shortName;
        FatNameScan scan;
        FatEntrySetWriter writer;
        infra::Function<void(Result result)> resume;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
