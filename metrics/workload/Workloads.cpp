#include "metrics/workload/Workloads.hpp"
#include "metrics/workload/Pattern.hpp"
#include "metrics/workload/Random.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

namespace metrics
{
    namespace
    {
        constexpr uint32_t kibibyte = 1024;
        constexpr uint32_t mebibyte = 1024 * kibibyte;
        constexpr uint32_t chunkSize = 4 * kibibyte;
        constexpr std::chrono::milliseconds loggerIdle{ 100 };

        uint32_t Scaled(uint32_t size, const WorkloadParameters& parameters)
        {
            return std::max<uint32_t>(size / parameters.scaleDivisor, 1);
        }

        std::string Path(std::string_view directory, uint32_t index)
        {
            return std::string{ directory } + "/" + std::to_string(index);
        }

        bool WriteFile(Runner& runner, std::string_view path, uint32_t fileId, uint32_t size, uint32_t chunk, bool announceSize = false)
        {
            auto file = runner.Open(path, OpenMode::write);
            if (!file)
                return false;

            if (announceSize)
                runner.ExpectSize(*file, size);

            std::vector<uint8_t> data(chunk);
            for (uint32_t offset = 0; offset < size && !runner.Interrupted(); offset += chunk)
            {
                auto part = infra::Head(infra::MakeRange(data), std::min(chunk, size - offset));
                FillPattern(part, fileId, offset);
                if (runner.Write(*file, part) != fs::Result::success)
                {
                    runner.Close(*file);
                    return false;
                }
            }

            runner.Sync(*file);
            return runner.Close(*file) == fs::Result::success;
        }

        void Prepare(Runner& runner, std::string_view workload)
        {
            runner.SetPhase(std::string{ workload } + " prepare");
            runner.Format();
            runner.Mount();
        }

        void Prepare(Runner& runner, std::string_view workload, const WorkloadParameters& parameters, uint64_t reservedBytes)
        {
            Prepare(runner, workload);

            if (parameters.fillPercent != 0)
                FillVolume(runner, parameters.fillPercent, parameters, reservedBytes);
        }

        void ReadFileInChunks(Runner& runner, std::string_view path)
        {
            auto file = runner.Open(path, OpenMode::read);
            if (!file)
                return;

            std::vector<uint8_t> data(chunkSize);
            while (!runner.Interrupted() && runner.Read(*file, infra::MakeRange(data)) == data.size())
            {
            }

            runner.Close(*file);
        }

        void ReadSequentially(Runner& runner, const WorkloadParameters& parameters)
        {
            auto size = Scaled(mebibyte, parameters);

            runner.SetPhase("L6 create");
            WriteFile(runner, "/read", 600, size, chunkSize);
            runner.SetPhase("L6 sequential");
            ReadFileInChunks(runner, "/read");
        }

        void ReadRandomly(Runner& runner, uint32_t size, Random& random)
        {
            runner.SetPhase(Phase("L6 random create", size));
            WriteFile(runner, "/random", 601, size, chunkSize);

            auto file = runner.Open("/random", OpenMode::read);
            if (!file)
                return;

            std::array<uint8_t, randomReadSize> data{};
            runner.SetPhase(Phase("L6 random", size));
            for (int read = 0; read != 64 && !runner.Interrupted(); ++read)
            {
                runner.Seek(*file, random.Below(size / randomReadSize) * randomReadSize);
                runner.Read(*file, infra::MakeRange(data));
            }

            runner.SetPhase(Phase("L6 seek end", size));
            runner.Seek(*file, size - randomReadSize);
            runner.Read(*file, infra::MakeRange(data));
            runner.Close(*file);
            runner.Remove("/random");
        }

        void LookUp(Runner& runner, uint32_t entries, Random& random)
        {
            auto directory = "/dir" + std::to_string(entries);

            runner.SetPhase(Phase("L6 directory create", entries));
            runner.MakeDirectory(directory);
            for (uint32_t index = 0; index != entries && !runner.Interrupted(); ++index)
                WriteFile(runner, Path(directory, index), 700 + index, 0, 1);

            runner.SetPhase(Phase("L6 list", entries));
            runner.List(directory);

            runner.SetPhase(Phase("L6 lookup", entries));
            for (int lookup = 0; lookup != 32 && !runner.Interrupted(); ++lookup)
                if (auto file = runner.Open(Path(directory, random.Below(entries)), OpenMode::read))
                    runner.Close(*file);
        }
    }

    std::string Phase(std::string_view workload, uint64_t value)
    {
        return std::string{ workload } + " " + std::to_string(value);
    }

    std::string Phase(std::string_view workload, uint64_t first, uint64_t second)
    {
        return Phase(workload, first) + " " + std::to_string(second);
    }

    uint64_t FillVolume(Runner& runner, uint8_t percent, const WorkloadParameters& parameters, uint64_t reservedBytes)
    {
        runner.SetPhase("fill");
        runner.MakeDirectory("/fill");

        auto fileSize = Scaled(64 * kibibyte, parameters);
        auto usage = runner.QueryUsage();
        if (!usage)
            return 0;

        auto target = (usage->totalBytes - std::min(usage->totalBytes, reservedBytes)) * percent / 100;
        uint64_t filled = 0;

        for (uint32_t index = 0; usage && usage->usedBytes + fileSize <= target; ++index)
        {
            if (!WriteFile(runner, Path("/fill", index), 1000 + index, fileSize, std::min(fileSize, chunkSize)))
                break;

            filled += fileSize;
            usage = runner.QueryUsage();
        }

        return filled;
    }

    void SequentialWrite(Runner& runner, const WorkloadParameters& parameters)
    {
        Prepare(runner, "L1");

        uint32_t fileId = 100;
        for (auto size : { mebibyte, 4 * mebibyte })
            for (auto chunk : { 64u, 512u, chunkSize })
            {
                auto scaledSize = Scaled(size, parameters);
                runner.SetPhase(Phase("L1", scaledSize, chunk));
                WriteFile(runner, "/sequential", fileId++, scaledSize, chunk, true);
                runner.Settle(std::chrono::minutes(10));

                runner.SetPhase(Phase("L1 cleanup", scaledSize, chunk));
                runner.Remove("/sequential");
                runner.Settle(std::chrono::minutes(10));
            }
    }

    void DataLogger(Runner& runner, const WorkloadParameters& parameters)
    {
        auto size = Scaled(4 * mebibyte, parameters);
        Prepare(runner, "L2", parameters, size);
        runner.SetPhase("L2");

        auto file = runner.Open("/log", OpenMode::append);
        if (!file)
            return;

        std::array<uint8_t, appendSize> record{};
        for (uint32_t offset = 0; offset + appendSize <= size && !runner.Interrupted(); offset += appendSize)
        {
            FillPattern(infra::MakeRange(record), 200, offset);
            if (runner.Write(*file, infra::MakeRange(record)) != fs::Result::success)
                break;

            runner.Sync(*file);
            runner.Idle(loggerIdle);
        }

        runner.Close(*file);
    }

    void RandomOverwrite(Runner& runner, const WorkloadParameters& parameters)
    {
        Prepare(runner, "L3", parameters, 2 * std::max(Scaled(4 * mebibyte, parameters), overwriteSize));

        Random random{ parameters.seed };
        uint32_t fileId = 300;
        for (auto size : { 64 * kibibyte, mebibyte, 4 * mebibyte })
        {
            auto scaledSize = std::max(Scaled(size, parameters), overwriteSize);
            runner.SetPhase(Phase("L3 create", scaledSize));
            if (!WriteFile(runner, "/overwrite", fileId, scaledSize, chunkSize))
                return;

            runner.SetPhase(Phase("L3", scaledSize));
            auto file = runner.Open("/overwrite", OpenMode::readWrite);
            if (!file)
                return;

            std::array<uint8_t, overwriteSize> data{};
            for (int overwrite = 0; overwrite != 64 && !runner.Interrupted(); ++overwrite)
            {
                auto offset = random.Below(scaledSize / overwriteSize) * overwriteSize;
                FillPattern(infra::MakeRange(data), fileId + 1, offset);
                runner.Seek(*file, offset);
                runner.Write(*file, infra::MakeRange(data));
                runner.Sync(*file);
            }

            runner.Close(*file);
            runner.SetPhase(Phase("L3 cleanup", scaledSize));
            runner.Remove("/overwrite");
            fileId += 2;
        }
    }

    void SmallFiles(Runner& runner, const WorkloadParameters& parameters)
    {
        constexpr uint32_t window = 16;
        constexpr uint32_t largestFile = 8 * kibibyte;

        Prepare(runner, "L4", parameters, window * largestFile);
        runner.SetPhase("L4");
        runner.MakeDirectory("/small");

        Random random{ parameters.seed + 4 };
        auto files = std::max<uint32_t>(200 / parameters.scaleDivisor, 4);
        for (uint32_t index = 0; index != files && !runner.Interrupted(); ++index)
        {
            auto size = 100 + random.Below(largestFile - 100 + 1);
            WriteFile(runner, Path("/small", index), 400 + index, size, size);

            if (index >= window)
                runner.Remove(Path("/small", index - window));
        }
    }

    void StaticFill(Runner& runner, const WorkloadParameters& parameters, const std::function<bool(uint32_t rewrites)>& keepRewriting)
    {
        Prepare(runner, "L5");
        FillVolume(runner, 70, parameters, 0);
        runner.SetPhase("L5");

        std::array<uint8_t, chunkSize> configuration{};
        for (uint32_t rewrite = 0; keepRewriting(rewrite) && !runner.Interrupted(); ++rewrite)
        {
            FillPattern(infra::MakeRange(configuration), 500, rewrite);

            auto file = runner.Open("/config", OpenMode::write);
            if (!file)
                return;

            runner.Write(*file, infra::MakeRange(configuration));
            runner.Close(*file);
        }
    }

    void ReadPatterns(Runner& runner, const WorkloadParameters& parameters)
    {
        Prepare(runner, "L6");
        ReadSequentially(runner, parameters);

        Random random{ parameters.seed + 6 };
        for (auto size : { 64 * kibibyte, mebibyte, 4 * mebibyte })
            ReadRandomly(runner, std::max(Scaled(size, parameters), randomReadSize), random);

        for (auto entries : { 10u, 100u, 1000u })
            LookUp(runner, entries, random);
    }

    void MountScaling(Runner& runner, const WorkloadParameters& parameters)
    {
        for (auto percent : { 10u, 50u, 90u })
        {
            runner.SetPhase(Phase("L7 prepare", percent));
            runner.Format();
            runner.Mount();
            FillVolume(runner, static_cast<uint8_t>(percent), parameters, 0);
            runner.Unmount();

            runner.SetPhase(Phase("L7", percent));
            runner.Mount();

            runner.SetPhase(Phase("L7 cleanup", percent));
            runner.Unmount();
        }
    }
}
