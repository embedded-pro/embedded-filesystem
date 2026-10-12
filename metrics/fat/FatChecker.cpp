#include "metrics/fat/FatChecker.hpp"
#include <algorithm>
#include <map>

namespace metrics
{
    namespace
    {
        constexpr uint32_t sectorBytes = 512;
        constexpr uint32_t entryBytes = 32;

        uint32_t Load16(const std::vector<uint8_t>& bytes, std::size_t offset)
        {
            return bytes[offset] | bytes[offset + 1] << 8u;
        }

        uint32_t Load32(const std::vector<uint8_t>& bytes, std::size_t offset)
        {
            return Load16(bytes, offset) | Load16(bytes, offset + 2) << 16u;
        }

        uint32_t BitsOf(uint32_t clusterCount)
        {
            if (clusterCount < 4085)
                return 12;

            if (clusterCount < 65525)
                return 16;

            return 32;
        }

        uint32_t MaskOf(uint32_t bits)
        {
            switch (bits)
            {
                case 12:
                    return 0xfff;
                case 16:
                    return 0xffff;
                default:
                    return 0x0fffffff;
            }
        }

        bool IsBootSector(const std::vector<uint8_t>& sector)
        {
            return sector[510] == 0x55 && sector[511] == 0xaa && (sector[0] == 0xeb || sector[0] == 0xe9) && Load16(sector, 11) == sectorBytes;
        }

        std::string Utf8Of(const std::vector<char16_t>& units)
        {
            std::string result;

            for (std::size_t index = 0; index != units.size(); ++index)
            {
                char32_t point = units[index];

                if (point >= 0xd800 && point < 0xdc00 && index + 1 != units.size())
                    point = 0x10000 + ((point - 0xd800) << 10u) + (units[++index] - 0xdc00);

                if (point < 0x80)
                    result += static_cast<char>(point);
                else if (point < 0x800)
                    result += { static_cast<char>(0xc0 | point >> 6), static_cast<char>(0x80 | (point & 0x3f)) };
                else if (point < 0x10000)
                    result += { static_cast<char>(0xe0 | point >> 12), static_cast<char>(0x80 | (point >> 6 & 0x3f)), static_cast<char>(0x80 | (point & 0x3f)) };
                else
                    result += { static_cast<char>(0xf0 | point >> 18), static_cast<char>(0x80 | (point >> 12 & 0x3f)), static_cast<char>(0x80 | (point >> 6 & 0x3f)), static_cast<char>(0x80 | (point & 0x3f)) };
            }

            return result;
        }

        std::string ShortNameOf(const uint8_t* entry)
        {
            auto part = [entry](std::size_t begin, std::size_t end, bool lower)
            {
                std::string text;

                for (auto index = begin; index != end && entry[index] != ' '; ++index)
                {
                    auto byte = index == 0 && entry[0] == 0x05 ? 0xe5 : entry[index];
                    text += static_cast<char>(lower && byte >= 'A' && byte <= 'Z' ? byte + 'a' - 'A' : byte);
                }

                return text;
            };

            auto base = part(0, 8, (entry[12] & 0x08) != 0);
            auto extension = part(8, 11, (entry[12] & 0x10) != 0);
            return extension.empty() ? base : base + "." + extension;
        }

        uint8_t ChecksumOf(const uint8_t* entry)
        {
            uint8_t sum = 0;

            for (std::size_t index = 0; index != 11; ++index)
                sum = static_cast<uint8_t>(((sum & 1) << 7) + (sum >> 1) + entry[index]);

            return sum;
        }

        class LongNameSet
        {
        public:
            void Feed(const uint8_t* entry)
            {
                auto ordinal = static_cast<uint32_t>(entry[0] & 0x3f);

                if ((entry[0] & 0x40) != 0)
                {
                    Abandon();
                    checksum = entry[13];
                    pieces.assign(ordinal, {});
                }
                else if (!expected || ordinal + 1 != *expected || entry[13] != checksum)
                {
                    Abandon();
                    ++orphans;
                    return;
                }

                if (ordinal == 0)
                {
                    ++orphans;
                    return;
                }

                expected = ordinal;
                pieces[ordinal - 1] = Units(entry);
            }

            std::optional<std::string> Complete(const uint8_t* shortEntry)
            {
                std::optional<std::string> name;

                if (expected == 1 && ChecksumOf(shortEntry) == checksum)
                    name = Joined();
                else if (expected)
                    ++orphans;

                expected = std::nullopt;
                return name;
            }

            void Abandon()
            {
                if (expected)
                    ++orphans;

                expected = std::nullopt;
            }

            uint32_t orphans{ 0 };

        private:
            static std::vector<char16_t> Units(const uint8_t* entry)
            {
                std::vector<char16_t> units;

                for (std::size_t offset : { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 })
                {
                    auto unit = static_cast<char16_t>(entry[offset] | entry[offset + 1] << 8u);

                    if (unit == 0 || unit == 0xffff)
                        break;

                    units.push_back(unit);
                }

                return units;
            }

            std::string Joined() const
            {
                std::vector<char16_t> units;

                for (const auto& piece : pieces)
                    units.insert(units.end(), piece.begin(), piece.end());

                return Utf8Of(units);
            }

            std::optional<uint32_t> expected;
            uint8_t checksum{ 0 };
            std::vector<std::vector<char16_t>> pieces;
        };

        struct PendingDirectory
        {
            std::string path;
            uint32_t cluster;
        };

        class Checker
        {
        public:
            Checker(const SectorReader& read, FatCheckMode mode)
                : read(read)
                , mode(mode)
            {}

            FatCheck Run()
            {
                if (ReadLayout() && ReadTables())
                {
                    WalkDirectories();
                    CheckChains();
                    CountLostClusters();
                }

                return std::move(check);
            }

        private:
            void Problem(const std::string& problem)
            {
                if (!check.problem)
                    check.problem = problem;
            }

            bool ReadLayout()
            {
                auto first = read(0);
                uint32_t start = 0;

                if (!IsBootSector(first))
                {
                    start = Load32(first, 446 + 8);
                    first = read(start);
                }

                if (!IsBootSector(first))
                {
                    Problem("no boot sector");
                    return false;
                }

                return Layout(first, start);
            }

            bool Layout(const std::vector<uint8_t>& boot, uint32_t start)
            {
                auto& layout = check.layout;
                auto totalSectors = Load16(boot, 19) != 0 ? Load16(boot, 19) : Load32(boot, 32);
                layout.volumeStart = start;
                layout.sectorsPerCluster = boot[13];
                layout.firstFatSector = start + Load16(boot, 14);
                layout.fatCopies = boot[16];
                layout.sectorsPerFat = Load16(boot, 22) != 0 ? Load16(boot, 22) : Load32(boot, 36);
                layout.rootSector = layout.firstFatSector + layout.fatCopies * layout.sectorsPerFat;
                layout.rootSectors = (Load16(boot, 17) * entryBytes + sectorBytes - 1) / sectorBytes;
                layout.firstDataSector = layout.rootSector + layout.rootSectors;

                if (layout.sectorsPerCluster == 0 || layout.firstDataSector - start >= totalSectors)
                {
                    Problem("invalid parameter block");
                    return false;
                }

                layout.clusterCount = (totalSectors - (layout.firstDataSector - start)) / layout.sectorsPerCluster;
                layout.bits = BitsOf(layout.clusterCount);
                layout.rootCluster = layout.bits == 32 ? Load32(boot, 44) : 0;
                return true;
            }

            bool ReadTables()
            {
                const auto& layout = check.layout;

                for (uint32_t copy = 0; copy != layout.fatCopies; ++copy)
                {
                    std::vector<uint8_t> bytes;

                    for (uint32_t sector = 0; sector != layout.sectorsPerFat; ++sector)
                    {
                        auto contents = read(layout.firstFatSector + copy * layout.sectorsPerFat + sector);
                        bytes.insert(bytes.end(), contents.begin(), contents.end());
                    }

                    tables.push_back(Decoded(bytes));
                }

                if (tables.empty() || tables.front().size() < layout.clusterCount + 2)
                {
                    Problem("FAT too small for its clusters");
                    return false;
                }

                for (std::size_t copy = 1; copy != tables.size(); ++copy)
                    if (mode == FatCheckMode::strict && !std::equal(tables[0].begin() + 2, tables[0].begin() + layout.clusterCount + 2, tables[copy].begin() + 2))
                        Problem("FAT copy " + std::to_string(copy) + " differs from the first");

                return true;
            }

            std::vector<uint32_t> Decoded(const std::vector<uint8_t>& bytes) const
            {
                std::vector<uint32_t> entries;
                auto bits = check.layout.bits;
                auto count = bytes.size() * 8 / bits;

                for (std::size_t cluster = 0; cluster != count; ++cluster)
                    if (bits == 12)
                    {
                        auto pair = Load16(bytes, cluster * 3 / 2);
                        entries.push_back(cluster % 2 == 0 ? pair & 0xfff : pair >> 4u);
                    }
                    else if (bits == 16)
                        entries.push_back(Load16(bytes, cluster * 2));
                    else
                        entries.push_back(Load32(bytes, cluster * 4) & 0x0fffffff);

                return entries;
            }

            bool IsEndOfChain(uint32_t value) const
            {
                return value >= (MaskOf(check.layout.bits) & ~7u);
            }

            bool IsBad(uint32_t value) const
            {
                return value == (MaskOf(check.layout.bits) & ~8u);
            }

            bool IsCluster(uint32_t value) const
            {
                return value >= 2 && value < check.layout.clusterCount + 2;
            }

            std::optional<std::vector<uint32_t>> Chain(uint32_t first, const std::string& owner)
            {
                std::vector<uint32_t> clusters;

                for (auto cluster = first; !IsEndOfChain(cluster); cluster = tables[0][cluster])
                {
                    if (!IsCluster(cluster) || tables[0][cluster] == 0 || IsBad(tables[0][cluster]))
                    {
                        Problem(owner + ": chain links to " + (IsCluster(cluster) ? "a free or bad cluster " : "a cluster out of range ") + std::to_string(cluster));
                        return std::nullopt;
                    }

                    if (clusters.size() == check.layout.clusterCount)
                    {
                        Problem(owner + ": chain loops");
                        return std::nullopt;
                    }

                    clusters.push_back(cluster);
                }

                return clusters;
            }

            void WalkDirectories()
            {
                std::vector<PendingDirectory> pending{ { "", check.layout.rootCluster } };

                if (check.layout.bits == 32)
                    if (auto chain = Chain(check.layout.rootCluster, "/"))
                        for (auto cluster : *chain)
                            Own(cluster, "/", nullptr);

                while (!pending.empty())
                {
                    auto directory = pending.back();
                    pending.pop_back();
                    ParseDirectory(directory, pending);
                }
            }

            std::vector<uint32_t> DirectorySectors(const PendingDirectory& directory)
            {
                std::vector<uint32_t> sectors;
                const auto& layout = check.layout;

                if (directory.cluster == 0)
                {
                    for (uint32_t sector = 0; sector != layout.rootSectors; ++sector)
                        sectors.push_back(layout.rootSector + sector);

                    return sectors;
                }

                if (auto chain = Chain(directory.cluster, directory.path.empty() ? "/" : directory.path))
                    for (auto cluster : *chain)
                        for (uint32_t sector = 0; sector != layout.sectorsPerCluster; ++sector)
                            sectors.push_back(layout.SectorOfCluster(cluster) + sector);

                return sectors;
            }

            void ParseDirectory(const PendingDirectory& directory, std::vector<PendingDirectory>& pending)
            {
                LongNameSet longName;

                for (auto sector : DirectorySectors(directory))
                {
                    auto contents = read(sector);

                    for (std::size_t offset = 0; offset != sectorBytes; offset += entryBytes)
                        if (!ParseEntry(&contents[offset], directory, longName, pending))
                        {
                            longName.Abandon();
                            check.orphanedLongNames += longName.orphans;
                            return;
                        }
                }

                longName.Abandon();
                check.orphanedLongNames += longName.orphans;
            }

            bool ParseEntry(const uint8_t* entry, const PendingDirectory& directory, LongNameSet& longName, std::vector<PendingDirectory>& pending)
            {
                if (entry[0] == 0)
                    return false;

                if (entry[0] == 0xe5)
                    longName.Abandon();
                else if ((entry[11] & 0x3f) == 0x0f)
                    longName.Feed(entry);
                else if ((entry[11] & 0x08) != 0 || entry[0] == '.')
                    longName.Abandon();
                else
                    AddEntry(entry, directory, longName.Complete(entry), pending);

                return true;
            }

            void AddEntry(const uint8_t* raw, const PendingDirectory& directory, const std::optional<std::string>& longName, std::vector<PendingDirectory>& pending)
            {
                std::vector<uint8_t> bytes(raw, raw + entryBytes);
                FatImageEntry entry;
                entry.path = directory.path + "/" + longName.value_or(ShortNameOf(raw));
                entry.directory = (raw[11] & 0x10) != 0;
                entry.firstCluster = Load16(bytes, 26) | (check.layout.bits == 32 ? Load16(bytes, 20) << 16u : 0);
                entry.size = entry.directory ? 0 : Load32(bytes, 28);

                if (entry.directory && IsCluster(entry.firstCluster))
                    pending.push_back({ entry.path, entry.firstCluster });
                else if (entry.directory)
                    Problem(entry.path + ": directory without a valid first cluster");

                check.entries.push_back(entry);
            }

            void CheckChains()
            {
                for (std::size_t index = 0; index != check.entries.size(); ++index)
                    CheckChain(index);
            }

            void CheckChain(std::size_t index)
            {
                auto& entry = check.entries[index];
                auto clusterBytes = check.layout.ClusterBytes();
                auto needed = (uint64_t{ entry.size } + clusterBytes - 1) / clusterBytes;

                if (entry.firstCluster == 0)
                {
                    if (needed != 0)
                        Problem(entry.path + ": a file of " + std::to_string(entry.size) + " bytes has no clusters");

                    return;
                }

                auto chain = Chain(entry.firstCluster, entry.path);
                if (!chain)
                    return;

                entry.clusters = *chain;

                if (chain->size() < needed)
                    Problem(entry.path + ": chain of " + std::to_string(chain->size()) + " clusters is shorter than its size of " + std::to_string(entry.size) + " bytes");
                else if (!entry.directory && chain->size() > needed && mode == FatCheckMode::strict)
                    Problem(entry.path + ": chain of " + std::to_string(chain->size()) + " clusters is longer than its size of " + std::to_string(entry.size) + " bytes");

                for (auto cluster : *chain)
                    Own(cluster, entry.path, &entry);
            }

            void Own(uint32_t cluster, const std::string& path, const FatImageEntry* entry)
            {
                auto [owner, added] = owners.emplace(cluster, entry);

                if (added)
                    return;

                auto alias = owner->second != nullptr && entry != nullptr && owner->second->firstCluster == entry->firstCluster;

                if (!alias || mode == FatCheckMode::strict)
                    Problem(path + ": cluster " + std::to_string(cluster) + " is also in the chain of " + (owner->second != nullptr ? owner->second->path : "/"));
            }

            void CountLostClusters()
            {
                for (uint32_t cluster = 2; cluster != check.layout.clusterCount + 2; ++cluster)
                    if (tables[0][cluster] != 0 && !IsBad(tables[0][cluster]) && owners.count(cluster) == 0)
                        ++check.lostClusters;

                if (mode == FatCheckMode::strict && check.lostClusters != 0)
                    Problem(std::to_string(check.lostClusters) + " lost clusters");

                if (mode == FatCheckMode::strict && check.orphanedLongNames != 0)
                    Problem(std::to_string(check.orphanedLongNames) + " orphaned long name entries");
            }

            const SectorReader& read;
            FatCheckMode mode;
            FatCheck check;
            std::vector<std::vector<uint32_t>> tables;
            std::map<uint32_t, const FatImageEntry*> owners;
        };
    }

    uint32_t FatImageLayout::SectorOfCluster(uint32_t cluster) const
    {
        return firstDataSector + (cluster - 2) * sectorsPerCluster;
    }

    uint32_t FatImageLayout::ClusterBytes() const
    {
        return sectorsPerCluster * sectorBytes;
    }

    const FatImageEntry* FatCheck::Find(const std::string& path) const
    {
        auto found = std::find_if(entries.begin(), entries.end(), [&path](const FatImageEntry& entry)
            {
                return entry.path == path;
            });

        return found != entries.end() ? &*found : nullptr;
    }

    FatCheck CheckFatImage(const SectorReader& read, FatCheckMode mode)
    {
        return Checker(read, mode).Run();
    }

    std::vector<uint8_t> ReadContents(const SectorReader& read, const FatCheck& check, const FatImageEntry& entry)
    {
        std::vector<uint8_t> contents;

        for (auto cluster : entry.clusters)
            for (uint32_t sector = 0; sector != check.layout.sectorsPerCluster && contents.size() < entry.size; ++sector)
            {
                auto bytes = read(check.layout.SectorOfCluster(cluster) + sector);
                contents.insert(contents.end(), bytes.begin(), bytes.end());
            }

        contents.resize(std::min<std::size_t>(contents.size(), entry.size));
        return contents;
    }
}
