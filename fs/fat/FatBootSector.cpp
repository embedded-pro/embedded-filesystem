#include "fs/fat/FatBootSector.hpp"
#include "fs/fat/FatBytes.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <array>
#include <bit>

namespace fs
{
    namespace
    {
        constexpr std::size_t sectorBytes = 512;
        constexpr std::array<uint8_t, 8> oemName{ 'M', 'S', 'W', 'I', 'N', '4', '.', '1' };
        constexpr std::array<uint8_t, 11> volumeLabel{ 'N', 'O', ' ', 'N', 'A', 'M', 'E', ' ', ' ', ' ', ' ' };
        constexpr std::array<uint8_t, 8> fat12Name{ 'F', 'A', 'T', '1', '2', ' ', ' ', ' ' };
        constexpr std::array<uint8_t, 8> fat16Name{ 'F', 'A', 'T', '1', '6', ' ', ' ', ' ' };
        constexpr std::array<uint8_t, 8> fat32Name{ 'F', 'A', 'T', '3', '2', ' ', ' ', ' ' };

        constexpr uint8_t extendedBootSignature = 0x29;
        constexpr uint8_t driveNumber = 0x80;
        constexpr uint16_t sectorsPerTrack = 63;
        constexpr uint16_t heads = 255;

        bool HasJump(infra::ConstByteRange sector)
        {
            return sector[0] == 0xeb || sector[0] == 0xe9 || sector[0] == 0xe8;
        }

        bool IsSectorSize(uint16_t bytes)
        {
            return bytes >= 512 && bytes <= 4096 && std::has_single_bit(bytes);
        }

        bool IsPlausible(const BiosParameterBlock& parameters)
        {
            return IsSectorSize(parameters.bytesPerSector) && std::has_single_bit(parameters.sectorsPerCluster) && parameters.reservedSectors != 0 && parameters.fatCopies != 0;
        }

        uint32_t TotalSectors(infra::ConstByteRange sector)
        {
            auto total16 = LoadLittle16(sector, 19);

            if (total16 != 0)
                return total16;

            return LoadLittle32(sector, 32);
        }

        void ParseFat32Fields(infra::ConstByteRange sector, BiosParameterBlock& parameters)
        {
            parameters.fat32Layout = true;
            parameters.sectorsPerFat = LoadLittle32(sector, 36);
            parameters.extendedFlags = LoadLittle16(sector, 40);
            parameters.fileSystemVersion = LoadLittle16(sector, 42);
            parameters.rootCluster = LoadLittle32(sector, 44);
            parameters.fsInfoSector = LoadLittle16(sector, 48);
            parameters.backupBootSector = LoadLittle16(sector, 50);
            parameters.volumeId = LoadLittle32(sector, 67);
        }

        void StoreBytes(infra::ByteRange sector, std::size_t offset, infra::ConstByteRange bytes)
        {
            infra::Copy(bytes, infra::Head(infra::DiscardHead(sector, offset), bytes.size()));
        }

        void StoreCommonFields(const BiosParameterBlock& parameters, FatType type, infra::ByteRange sector)
        {
            sector[0] = 0xeb;
            sector[1] = type == FatType::fat32 ? 0x58 : 0x3c;
            sector[2] = 0x90;
            StoreBytes(sector, 3, infra::MakeRange(oemName));
            StoreLittle16(sector, 11, parameters.bytesPerSector);
            sector[13] = parameters.sectorsPerCluster;
            StoreLittle16(sector, 14, parameters.reservedSectors);
            sector[16] = parameters.fatCopies;
            StoreLittle16(sector, 17, parameters.rootEntries);
            sector[21] = parameters.media;
            StoreLittle16(sector, 24, sectorsPerTrack);
            StoreLittle16(sector, 26, heads);
            StoreLittle32(sector, 28, parameters.hiddenSectors);

            if (type != FatType::fat32 && parameters.totalSectors <= 0xffff)
                StoreLittle16(sector, 19, static_cast<uint16_t>(parameters.totalSectors));
            else
                StoreLittle32(sector, 32, parameters.totalSectors);
        }

        void StoreExtendedFields(const BiosParameterBlock& parameters, infra::ConstByteRange name, std::size_t offset, infra::ByteRange sector)
        {
            sector[offset] = driveNumber;
            sector[offset + 2] = extendedBootSignature;
            StoreLittle32(sector, offset + 3, parameters.volumeId);
            StoreBytes(sector, offset + 7, infra::MakeRange(volumeLabel));
            StoreBytes(sector, offset + 18, name);
        }

        void StoreFat32Fields(const BiosParameterBlock& parameters, infra::ByteRange sector)
        {
            StoreLittle32(sector, 36, parameters.sectorsPerFat);
            StoreLittle16(sector, 40, parameters.extendedFlags);
            StoreLittle16(sector, 42, parameters.fileSystemVersion);
            StoreLittle32(sector, 44, parameters.rootCluster);
            StoreLittle16(sector, 48, parameters.fsInfoSector);
            StoreLittle16(sector, 50, parameters.backupBootSector);
            StoreExtendedFields(parameters, infra::MakeRange(fat32Name), 64, sector);
        }

        void StoreFat16Fields(const BiosParameterBlock& parameters, FatType type, infra::ByteRange sector)
        {
            StoreLittle16(sector, 22, static_cast<uint16_t>(parameters.sectorsPerFat));
            StoreExtendedFields(parameters, infra::MakeRange(type == FatType::fat12 ? fat12Name : fat16Name), 36, sector);
        }
    }

    std::optional<BiosParameterBlock> ParseBootSector(infra::ConstByteRange sector)
    {
        if (!HasBootSignature(sector) || !HasJump(sector))
            return std::nullopt;

        BiosParameterBlock parameters;
        parameters.bytesPerSector = LoadLittle16(sector, 11);
        parameters.sectorsPerCluster = sector[13];
        parameters.reservedSectors = LoadLittle16(sector, 14);
        parameters.fatCopies = sector[16];
        parameters.rootEntries = LoadLittle16(sector, 17);
        parameters.totalSectors = TotalSectors(sector);
        parameters.media = sector[21];
        parameters.sectorsPerFat = LoadLittle16(sector, 22);
        parameters.hiddenSectors = LoadLittle32(sector, 28);

        if (parameters.sectorsPerFat == 0)
            ParseFat32Fields(sector, parameters);
        else
            parameters.volumeId = LoadLittle32(sector, 39);

        if (!IsPlausible(parameters))
            return std::nullopt;

        return parameters;
    }

    void SerializeBootSector(const BiosParameterBlock& parameters, FatType type, infra::ByteRange sector)
    {
        really_assert(sector.size() >= sectorBytes && parameters.fat32Layout == (type == FatType::fat32));

        std::ranges::fill(sector, uint8_t{ 0 });
        StoreCommonFields(parameters, type, sector);

        if (type == FatType::fat32)
            StoreFat32Fields(parameters, sector);
        else
            StoreFat16Fields(parameters, type, sector);

        StoreBootSignature(sector);
    }
}
