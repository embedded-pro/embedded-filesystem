#ifndef SPIKE_FAT_READER_FAT_READER_SCAN_HPP
#define SPIKE_FAT_READER_FAT_READER_SCAN_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "spike/fat_reader/FatReaderLayout.hpp"
#include "spike/fat_reader/FatReaderWindow.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace spike
{
    struct FatPosition
    {
        uint32_t cluster{ 0 };
        uint32_t sector{ 0 };
        uint32_t entry{ 0 };
    };

    struct FatEntry
    {
        infra::BoundedConstString name;
        infra::BoundedConstString shortName;
        uint32_t firstCluster{ 0 };
        uint32_t size{ 0 };
        bool directory{ false };
    };

    class FatScan
    {
    public:
        FatScan(const FatLayout& layout, FatWindow& window, FatChain& chain);
        FatScan(const FatScan& other) = delete;
        FatScan& operator=(const FatScan& other) = delete;
        ~FatScan() = default;

        FatPosition Root() const;
        void Next(FatPosition& position, const infra::Function<void(fs::Result result, std::optional<FatEntry> entry)>& onDone);

    private:
        enum class Visit : uint8_t
        {
            skipped,
            found,
            end
        };

        uint32_t SectorOf(const FatPosition& at) const;
        void Load();
        void Loaded(fs::Result result);
        Visit VisitEntry(infra::ConstByteRange entry);
        void AddLongEntry(infra::ConstByteRange entry);
        void ForgetLongName();
        void Found(infra::ConstByteRange entry);
        bool UseLongName();
        void FormatShortName(infra::ConstByteRange entry);
        void Advance();
        void NextCluster(fs::Result result, uint32_t next);
        void End(fs::Result result);

    private:
        const FatLayout& layout;
        FatWindow& window;
        FatChain& chain;
        FatPosition* position{ nullptr };
        std::array<char16_t, 260> longName{};
        uint16_t longUnits{ 0 };
        uint8_t longNext{ 0 };
        uint8_t longChecksum{ 0 };
        bool longComplete{ false };
        infra::BoundedString::WithStorage<255> name;
        infra::BoundedString::WithStorage<36> shortName;
        infra::AutoResetFunction<void(fs::Result result, std::optional<FatEntry> entry)> onDone;
    };
}

#endif
