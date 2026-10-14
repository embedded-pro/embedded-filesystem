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
    template<NorConfig Config, std::size_t Files = 1>
    class ExposedNorFileSystem
        : public NorFileSystem::WithStorage<Config, Files>
    {
    public:
        using NorFileSystem::WithStorage<Config, Files>::WithStorage;
        using NorFileSystem::Volume;
    };

    template<NorConfig Config, uint32_t Sectors, uint32_t SectorSize>
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
        NorFlashStubImage image{ flash };
        std::optional<NorImageBuilder> builder;
        ExposedNorFileSystem<Config> fileSystem{ flash };
    };
}

#endif
