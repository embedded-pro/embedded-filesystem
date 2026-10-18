#ifndef FS_FAT_LITE_FAT_LITE_SCAN_HPP
#define FS_FAT_LITE_FAT_LITE_SCAN_HPP

#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatLiteScan
        : private FatDirectoryVisitor
    {
    public:
        explicit FatLiteScan(FatDirectoryReader& reader);
        FatLiteScan(const FatLiteScan& other) = delete;
        FatLiteScan& operator=(const FatLiteScan& other) = delete;
        ~FatLiteScan();

        void Scan(uint32_t directory, infra::BoundedConstString name, const infra::Function<void(Result result)>& onDone);

        const std::optional<FatScannedEntry>& Found() const;
        const std::optional<FatEntryPosition>& Free() const;
        bool Extensible() const;

    private:
        bool Visit(const FatScannedEntry& entry) override;
        void FreeSlot(FatEntryPosition position, bool end) override;
        void Scanned(Result result);

    private:
        FatDirectoryReader& reader;
        infra::BoundedConstString name;
        FatDirectoryCursor cursor;
        std::optional<FatScannedEntry> found;
        std::optional<FatEntryPosition> free;
        bool extensible{ false };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
