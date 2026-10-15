#ifndef FS_NOR_TEST_NOR_VOLUME_FIXTURE_HPP
#define FS_NOR_TEST_NOR_VOLUME_FIXTURE_HPP

#include "fs/nor/NorFileSystem.hpp"
#include "fs/nor/NorVolume.hpp"
#include "fs/nor/test_doubles/NorFlashStubImage.hpp"
#include "fs/nor/test_doubles/NorImageBuilder.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace fs
{
    class RecordingFlash
        : public hal::Flash
    {
    public:
        struct Request
        {
            uint32_t address{ 0 };
            std::size_t size{ 0 };
            bool erase{ false };

            bool operator==(const Request& other) const = default;
        };

        using Requests = infra::BoundedVector<Request>::WithMaxSize<64>;

        explicit RecordingFlash(hal::Flash& flash)
            : flash(flash)
        {}

        uint32_t NumberOfSectors() const override
        {
            return flash.NumberOfSectors();
        }

        uint32_t SizeOfSector(uint32_t sectorIndex) const override
        {
            return flash.SizeOfSector(sectorIndex);
        }

        uint32_t SectorOfAddress(uint32_t address) const override
        {
            return flash.SectorOfAddress(address);
        }

        uint32_t AddressOfSector(uint32_t sectorIndex) const override
        {
            return flash.AddressOfSector(sectorIndex);
        }

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override
        {
            Record(Request{ address, buffer.size(), false });
            flash.WriteBuffer(buffer, address, onDone);
        }

        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override
        {
            flash.ReadBuffer(buffer, address, onDone);
        }

        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override
        {
            Record(Request{ flash.AddressOfSector(beginIndex), endIndex - beginIndex, true });
            flash.EraseSectors(beginIndex, endIndex, onDone);
        }

        void Record(const Request& request)
        {
            if (recording && !requests.full())
                requests.push_back(request);
        }

        bool recording{ false };
        Requests requests;

    private:
        hal::Flash& flash;
    };

    template<NorConfig Config, std::size_t Files = 1>
    class ExposedNorFileSystem
        : public NorFileSystem::WithStorage<Config, Files>
    {
    public:
        using NorFileSystem::WithStorage<Config, Files>::WithStorage;
        using NorFileSystem::Volume;
    };

    template<NorConfig Config, uint32_t Sectors, uint32_t SectorSize, std::size_t Files = 1>
    class NorVolumeFixture
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        NorVolumeFixture()
        {
            fileSystem.Format([](Result) {});
            ExecuteAllActions();
        }

        NorImageBuilder& Builder()
        {
            if (!builder.has_value())
                builder.emplace(image, Config);

            return *builder;
        }

        void Mount(Result expected = Result::success)
        {
            builder.reset();
            testing::StrictMock<infra::MockCallback<void(Result)>> mounted;
            fileSystem.Mount([&mounted](Result result)
                {
                    mounted.callback(result);
                });
            EXPECT_CALL(mounted, callback(expected));
            ExecuteAllActions();
        }

        void Unmount(Result expected = Result::success)
        {
            testing::StrictMock<infra::MockCallback<void(Result)>> unmounted;
            fileSystem.Unmount([&unmounted](Result result)
                {
                    unmounted.callback(result);
                });
            EXPECT_CALL(unmounted, callback(expected));
            ExecuteAllActions();
        }

        Result MakeDirectory(infra::BoundedConstString path)
        {
            std::optional<Result> made;
            fileSystem.MakeDirectory(path, [&made](Result result)
                {
                    made = result;
                });

            EXPECT_FALSE(made.has_value());
            ExecuteAllActions();
            return made.value_or(Result::busy);
        }

        std::pair<Result, EntryInfo> Stat(infra::BoundedConstString path)
        {
            std::optional<std::pair<Result, EntryInfo>> found;
            fileSystem.Stat(path, [&found](Result result, const EntryInfo& info)
                {
                    found.emplace(result, EntryInfo{ {}, info.type, info.size });
                });

            ExecuteAllActions();
            return found.value_or(std::make_pair(Result::busy, EntryInfo{}));
        }

        using Names = infra::BoundedVector<infra::BoundedString::WithStorage<16>>::WithMaxSize<64>;

        Names List(infra::BoundedConstString path)
        {
            Names names;
            infra::SharedPtr<Directory> opened;
            fileSystem.OpenDirectory(path, [&opened](Result, infra::SharedPtr<Directory> directory)
                {
                    opened = directory;
                });
            ExecuteAllActions();

            bool more = opened != nullptr;
            while (more)
            {
                opened->Next([&names, &more](Result result, std::optional<EntryInfo> info)
                    {
                        more = result == Result::success && info.has_value();
                        if (more)
                            names.emplace_back(info->name);
                    });
                ExecuteAllActions();
            }

            return names;
        }

        NorVolume& Volume()
        {
            return fileSystem.Volume();
        }

        NorVolumeState& State()
        {
            return Volume().State();
        }

        NorBlockHeader HeaderOf(uint32_t block) const
        {
            std::array<uint8_t, 64> header{};
            constexpr auto headerSize = NorHeaderSize(Config.programUnit);
            auto bytes = infra::Head(infra::MakeRange(header), headerSize);
            image.Read(block * SectorSize * Config.sectorsPerBlock, bytes);
            return ParseNorBlockHeader(bytes, Config.programUnit);
        }

        hal::FlashStub flash{ Sectors, SectorSize };
        RecordingFlash device{ flash };
        NorFlashStubImage image{ flash };
        std::optional<NorImageBuilder> builder;
        ExposedNorFileSystem<Config, Files> fileSystem{ device };
    };
}

#endif
