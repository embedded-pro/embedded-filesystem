#ifndef FS_FAT_FAT_NAME_SCAN_HPP
#define FS_FAT_FAT_NAME_SCAN_HPP

#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatShortNameGeneration.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatFreeSlots
    {
        FatEntryPosition first;
        uint32_t index;
        bool extensible;

        bool operator==(const FatFreeSlots& other) const;
    };

    class FatNameScan
        : private FatDirectoryVisitor
    {
    public:
        explicit FatNameScan(FatDirectoryReader& reader);
        FatNameScan(const FatNameScan& other) = delete;
        FatNameScan& operator=(const FatNameScan& other) = delete;
        ~FatNameScan();

        void Scan(uint32_t directory, infra::BoundedConstString name, uint8_t slots, const std::optional<ShortNameBytes>& basis, const std::optional<FatEntryPosition>& except, const infra::Function<void(Result result)>& onDone);
        void ScanForAlias(const ShortNameBytes& alias, const infra::Function<void(Result result)>& onDone);

        const std::optional<FatScannedEntry>& Found() const;
        std::optional<uint32_t> FreeTail() const;
        bool AliasInUse() const;
        std::optional<FatFreeSlots> Slots() const;

    private:
        bool Visit(const FatScannedEntry& entry) override;
        void FreeSlot(FatEntryPosition position, bool end) override;

        void Read();
        void OnScanned(Result result);
        void MarkTail(const ShortNameBytes& name);
        bool UsesAlias(const FatScannedEntry& entry) const;
        uint32_t IndexOf(FatEntryPosition position) const;

    private:
        FatDirectoryReader& reader;
        uint32_t directory{ 0 };
        infra::BoundedConstString name;
        uint8_t slots{ 1 };
        std::optional<ShortNameBytes> basis;
        std::optional<FatEntryPosition> except;
        std::optional<ShortNameBytes> alias;
        FatDirectoryCursor cursor;
        std::optional<FatScannedEntry> found;
        uint32_t usedTails{ 0 };
        bool aliasInUse{ false };
        FatFreeSlots run{};
        uint32_t runLength{ 0 };
        std::optional<FatFreeSlots> freeSlots;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
