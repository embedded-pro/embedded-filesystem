#include "fs/fat/lite/FatLiteScan.hpp"

namespace fs
{
    FatLiteScan::FatLiteScan(FatDirectoryReader& reader)
        : reader(reader)
    {}

    FatLiteScan::~FatLiteScan() = default;

    void FatLiteScan::Scan(uint32_t directory, infra::BoundedConstString name, const infra::Function<void(Result result)>& onDone)
    {
        this->name = name;
        this->onDone = onDone;
        found = std::nullopt;
        free = std::nullopt;
        extensible = false;
        cursor = reader.Start(directory);
        reader.Read(cursor, *this, [this](Result result)
            {
                Scanned(result);
            });
    }

    const std::optional<FatScannedEntry>& FatLiteScan::Found() const
    {
        return found;
    }

    const std::optional<FatEntryPosition>& FatLiteScan::Free() const
    {
        return free;
    }

    bool FatLiteScan::Extensible() const
    {
        return extensible;
    }

    bool FatLiteScan::Visit(const FatScannedEntry& entry)
    {
        if (!NameMatches(entry, name))
            return true;

        found = entry;
        return false;
    }

    void FatLiteScan::FreeSlot(FatEntryPosition position, bool)
    {
        if (!free)
            free = position;
    }

    void FatLiteScan::Scanned(Result result)
    {
        if (result == Result::success && !found && !free && cursor.cluster != 0)
        {
            free = FatEntryPosition{ cursor.cluster, reader.Capacity(cursor.cluster) };
            extensible = true;
        }

        onDone(result);
    }
}
