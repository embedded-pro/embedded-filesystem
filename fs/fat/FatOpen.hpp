#ifndef FS_FAT_FAT_OPEN_HPP
#define FS_FAT_FAT_OPEN_HPP

#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatEntrySetWriter.hpp"
#include "fs/fat/FatNameScan.hpp"
#include "fs/fat/FatPathSlot.hpp"
#include "fs/fat/FatShortName.hpp"
#include "fs/fat/FatShortNameGeneration.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include <optional>

namespace fs
{
    class FatFile;
    class FatVolume;

    class FatOpen
    {
    public:
        using Allocate = infra::Function<infra::SharedPtr<FatFile>()>;

        FatOpen(FatVolume& volume, const FatOpenCall& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished);
        FatOpen(const FatOpen& other) = delete;
        FatOpen& operator=(const FatOpen& other) = delete;
        ~FatOpen();

        void Start();

    private:
        void OnParent(Result result);
        Result VerifyParent(Result result) const;
        void OnScanned(Result result);
        void OpenExisting(const FatScannedEntry& found);
        Result VerifyFile(const FatScannedEntry& found) const;
        void Create();
        void OnAliasScanned(Result result);
        void CreateWith(const ShortName& shortName);
        void OnCreated(Result result);
        void Finish(Result result);

    private:
        FatVolume& volume;
        FatOpenCall call;
        Allocate allocate;
        infra::Function<void(Result result)> onFinished;
        infra::BoundedConstString name;
        std::optional<ShortName> exact;
        ShortNameBytes alias{};
        FatNameScan scan;
        FatEntrySetWriter writer;
        infra::SharedPtr<FatFile> file;
    };
}

#endif
