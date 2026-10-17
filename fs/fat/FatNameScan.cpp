#include "fs/fat/FatNameScan.hpp"
#include "fs/fat/FatLongNames.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <bit>

namespace fs
{
    namespace
    {
        constexpr uint32_t tails = 32;
    }

    bool FatFreeSlots::operator==(const FatFreeSlots& other) const = default;

    FatNameScan::FatNameScan(FatDirectoryReader& reader, const FatLongNames* longNames)
        : reader(reader)
        , longNames(longNames)
    {}

    FatNameScan::~FatNameScan() = default;

    void FatNameScan::Scan(uint32_t directory, infra::BoundedConstString name, uint8_t slots, const std::optional<ShortNameBytes>& basis, const std::optional<FatEntryPosition>& except, const infra::Function<void(Result result)>& onDone)
    {
        this->directory = directory;
        this->name = name;
        this->slots = slots;
        this->basis = basis;
        this->except = except;
        this->onDone = onDone;
        alias = std::nullopt;
        found = std::nullopt;
        usedTails = 0;
        runLength = 0;
        freeSlots = std::nullopt;
        Read();
    }

    void FatNameScan::ScanForAlias(const ShortNameBytes& alias, const infra::Function<void(Result result)>& onDone)
    {
        really_assert(freeSlots.has_value());

        this->alias = alias;
        this->onDone = onDone;
        aliasInUse = false;
        Read();
    }

    const std::optional<FatScannedEntry>& FatNameScan::Found() const
    {
        return found;
    }

    std::optional<uint32_t> FatNameScan::FreeTail() const
    {
        auto tail = static_cast<uint32_t>(std::countr_one(usedTails)) + 1;

        if (tail > tails)
            return std::nullopt;

        return tail;
    }

    bool FatNameScan::AliasInUse() const
    {
        return aliasInUse;
    }

    std::optional<FatFreeSlots> FatNameScan::Slots() const
    {
        return freeSlots;
    }

    bool FatNameScan::Visit(const FatScannedEntry& entry)
    {
        if (alias)
        {
            aliasInUse = UsesAlias(entry);
            return !aliasInUse;
        }

        if (NameMatches(entry, name) && except != entry.shortEntry)
        {
            found = entry;
            return false;
        }

        if (basis)
        {
            MarkTail(entry.entry.name.bytes);

            if (entry.longName)
                if (auto form = ShortFormOf(entry.name))
                    MarkTail(*form);
        }

        return true;
    }

    void FatNameScan::FreeSlot(FatEntryPosition position, bool end)
    {
        if (freeSlots)
            return;

        auto index = IndexOf(position);

        if (runLength == 0 || position.cluster != run.first.cluster || index != run.index + runLength)
        {
            run = FatFreeSlots{ position, index, false };
            runLength = 0;
        }

        if (!end)
        {
            if (++runLength == slots)
                freeSlots = run;
        }
        else if (runLength + reader.Capacity(position.cluster) - position.index >= slots)
            freeSlots = run;
        else if (position.cluster != 0)
            freeSlots = FatFreeSlots{ position, index, true };
    }

    void FatNameScan::Read()
    {
        cursor = reader.Start(directory);
        reader.Read(cursor, *this, [this](Result result)
            {
                OnScanned(result);
            });
    }

    void FatNameScan::OnScanned(Result result)
    {
        if (result == Result::success && !found && !freeSlots && cursor.cluster != 0)
        {
            auto capacity = reader.Capacity(cursor.cluster);
            freeSlots = FatFreeSlots{ FatEntryPosition{ cursor.cluster, capacity }, cursor.clustersVisited * capacity, true };
        }

        onDone(result);
    }

    void FatNameScan::MarkTail(const ShortNameBytes& name)
    {
        if (auto tail = longNames->tailOf(name, *basis); tail.has_value() && *tail <= tails)
            usedTails |= 1u << (*tail - 1);
    }

    bool FatNameScan::UsesAlias(const FatScannedEntry& entry) const
    {
        return entry.entry.name.bytes == *alias || (entry.longName && ShortFormOf(entry.name) == alias);
    }

    uint32_t FatNameScan::IndexOf(FatEntryPosition position) const
    {
        return cursor.clustersVisited * reader.Capacity(position.cluster) + position.index;
    }
}
