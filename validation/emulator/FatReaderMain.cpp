#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "fs/fat/reader/FatReader.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/cortex_m/SystemTickTimerService.hpp"
#include "infra/event/EventDispatcherWithWeakPtr.hpp"
#include "validation/emulator/Emulator.hpp"
#include "validation/firmware/devices/RamBlockDevice.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace
{
    using validation::emulator::Check;
    using validation::emulator::Print;

    constexpr uint32_t diskBlocks = 256;
    constexpr std::size_t fileBytes = 3000;

    hal::cortex::InterruptTable::WithStorage<16> interruptTable;
    infra::EventDispatcherWithWeakPtr::WithSize<50> dispatcher;
    hal::cortex::SystemTickTimerService systemTick{ 25000000 };
    std::array<uint8_t, diskBlocks * validation::RamBlockDevice::blockSize> diskStorage;
    validation::RamBlockDevice disk{ infra::MakeRange(diskStorage) };
    fs::FatFormatter::WithStorage<8> formatter{ disk };
    fs::FatFileSystem::WithStorage<1, 4> writer{ disk };
    fs::FatReader::WithStorage<1> reader{ disk };

    std::array<uint8_t, fileBytes> content;
    alignas(4) std::array<uint8_t, fileBytes> readBack;
    std::optional<fs::Result> result;
    std::size_t bytesRead = 0;
    fs::EntryInfo info;
    infra::SharedPtr<fs::File> file;
    infra::SharedPtr<fs::Directory> directory;
    std::optional<fs::EntryInfo> entry;

    void Store(fs::Result stored)
    {
        result = stored;
    }

    bool Succeeds()
    {
        dispatcher.ExecuteUntil([]
            {
                return result.has_value();
            });

        return std::exchange(result, std::nullopt) == fs::Result::success;
    }

    bool Opens(fs::FileSystem& volume, const char* path, fs::OpenMode mode)
    {
        volume.Open(path, mode, [](fs::Result opened, infra::SharedPtr<fs::File> handle)
            {
                file = handle;
                result = opened;
            });

        return Succeeds();
    }

    bool Writes(const char* path, infra::ConstByteRange data)
    {
        if (!Opens(writer, path, fs::OpenMode::write))
            return false;

        file->Write(data, &Store);
        auto written = Succeeds();
        file->Close(&Store);
        auto closed = Succeeds();
        file = nullptr;
        return written && closed;
    }

    void PrepareTheDisk()
    {
        for (std::size_t i = 0; i != content.size(); ++i)
            content[i] = static_cast<uint8_t>(i * 7 + 3);

        formatter.Format(fs::FatFormatConfig{}, &Store);
        Check(Succeeds(), "the disk is formatted");
        writer.Mount(&Store);
        Check(Succeeds(), "the writer mounts the disk");
        writer.MakeDirectory("/data", &Store);
        Check(Succeeds(), "the writer makes a directory");
        Check(Writes("/data/a-file-with-a-long-name.bin", infra::MakeRange(content)), "the writer writes a file with a long name");
        Check(Writes("/short.txt", infra::Head(infra::MakeRange(content), 10)), "the writer writes a file with a short name");
        writer.Unmount(&Store);
        Check(Succeeds(), "the writer unmounts the disk");
    }

    void PrintPeak(const char* after)
    {
        Print(after, static_cast<uint32_t>(reader.StackPeak()));
    }

    bool Reads(std::size_t bytes)
    {
        file->Read(infra::Head(infra::DiscardHead(infra::MakeRange(readBack), bytesRead), bytes), [](fs::Result done, std::size_t count)
            {
                bytesRead += count;
                result = done;
            });

        return Succeeds();
    }

    bool ListsTheFile()
    {
        reader.OpenDirectory("/data", [](fs::Result opened, infra::SharedPtr<fs::Directory> handle)
            {
                directory = handle;
                result = opened;
            });

        if (!Succeeds())
            return false;

        std::size_t entries = 0;
        bool listed = false;

        do
        {
            directory->Next([](fs::Result next, std::optional<fs::EntryInfo> found)
                {
                    entry = found;
                    result = next;
                });

            if (!Succeeds())
                return false;

            if (entry && entry->name == "a-file-with-a-long-name.bin" && entry->size == fileBytes)
                listed = true;

            entries += entry ? 1 : 0;
        } while (entry);

        directory = nullptr;
        return listed && entries == 1;
    }

    void ReadTheDisk()
    {
        reader.Mount(&Store);
        Check(Succeeds(), "the reader mounts the disk");
        PrintPeak("stack peak after mounting ");

        reader.Stat("/data/a-file-with-a-long-name.bin", [](fs::Result found, const fs::EntryInfo& stat)
            {
                info = stat;
                result = found;
            });
        Check(Succeeds() && info.size == fileBytes && info.type == fs::EntryType::file, "the reader finds a file by its long name in a directory");
        PrintPeak("stack peak after a stat ");

        Check(Opens(reader, "/data/a-file-with-a-long-name.bin", fs::OpenMode::read), "the reader opens the file");
        Check(Reads(1024) && Reads(1000) && Reads(fileBytes) && bytesRead == fileBytes, "the reader reads whole sectors and parts of sectors");
        Check(std::equal(readBack.begin(), readBack.end(), content.begin()), "the reader reads what the writer wrote");
        file = nullptr;
        PrintPeak("stack peak after reading ");

        Check(ListsTheFile(), "the reader lists the directory");
        PrintPeak("stack peak after listing ");

        reader.Unmount(&Store);
        Check(Succeeds(), "the reader unmounts the disk");
        Check(reader.StackPeak() + 384 <= fs::Fiber::defaultStackBytes, "the reader leaves 384 bytes of its stack to the device and the interrupts");
    }
}

int main()
{
    PrepareTheDisk();
    ReadTheDisk();
    return 0;
}
