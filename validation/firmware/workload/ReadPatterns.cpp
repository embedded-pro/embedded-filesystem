#include "validation/firmware/workload/ReadPatterns.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include <algorithm>

namespace validation
{
    ReadPatterns::ReadPatterns(WorkloadTools& tools)
        : tools(tools)
    {}

    infra::BoundedConstString ReadPatterns::Name() const
    {
        return "L6";
    }

    bool ReadPatterns::Fills() const
    {
        return false;
    }

    void ReadPatterns::Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone)
    {
        this->parameters = parameters;
        this->parameters.fillPercent = 0;
        this->onDone = onDone;
        tools.preparation.Prepare(Name(), this->parameters, 0, [this]
            {
                CreateSequential();
            });
    }

    void ReadPatterns::CreateSequential()
    {
        tools.runner.SetPhase("L6,create");
        tools.writer.Write({ "/read", 600, Scaled(mebibyte, parameters), chunkSize }, [this](bool)
            {
                tools.runner.SetPhase("L6,sequential");
                tools.runner.Open("/read", fs::OpenMode::read, [this](fs::Result result)
                    {
                        if (result == fs::Result::success)
                            ReadChunk();
                        else
                            StartRandom();
                    });
            });
    }

    void ReadPatterns::ReadChunk()
    {
        tools.runner.Read(infra::MakeRange(data), [this](fs::Result, std::size_t bytesRead)
            {
                if (bytesRead == data.size())
                    ReadChunk();
                else
                    tools.runner.Close([this](fs::Result)
                        {
                            StartRandom();
                        });
            });
    }

    void ReadPatterns::StartRandom()
    {
        random = metrics::Random{ parameters.seed + 6 };
        step = 0;
        CreateRandom();
    }

    uint32_t ReadPatterns::RandomFileSize() const
    {
        return std::max(Scaled(randomFileSizes[step], parameters), randomReadSize);
    }

    void ReadPatterns::CreateRandom()
    {
        if (step == randomFileSizes.size())
        {
            step = 0;
            CreateDirectory();
            return;
        }

        tools.runner.SetPhase(MakePhase("L6,random,create", RandomFileSize()));
        tools.writer.Write({ "/random", 601, RandomFileSize(), chunkSize }, [this](bool)
            {
                OpenRandom();
            });
    }

    void ReadPatterns::OpenRandom()
    {
        tools.runner.Open("/random", fs::OpenMode::read, [this](fs::Result result)
            {
                if (result != fs::Result::success)
                {
                    ++step;
                    CreateRandom();
                    return;
                }

                tools.runner.SetPhase(MakePhase("L6,random", RandomFileSize()));
                count = 0;
                ReadAtRandom();
            });
    }

    void ReadPatterns::ReadAtRandom()
    {
        if (count == randomReads)
        {
            ReadAtEnd();
            return;
        }

        tools.runner.Seek(random.Below(RandomFileSize() / randomReadSize) * randomReadSize);
        tools.runner.Read(infra::Head(infra::MakeRange(data), randomReadSize), [this](fs::Result, std::size_t)
            {
                ++count;
                ReadAtRandom();
            });
    }

    void ReadPatterns::ReadAtEnd()
    {
        tools.runner.SetPhase(MakePhase("L6,seek,end", RandomFileSize()));
        tools.runner.Seek(RandomFileSize() - randomReadSize);
        tools.runner.Read(infra::Head(infra::MakeRange(data), randomReadSize), [this](fs::Result, std::size_t)
            {
                tools.runner.Close([this](fs::Result)
                    {
                        NextRandomFile();
                    });
            });
    }

    void ReadPatterns::NextRandomFile()
    {
        tools.runner.Remove("/random", [this](fs::Result)
            {
                ++step;
                CreateRandom();
            });
    }

    void ReadPatterns::CreateDirectory()
    {
        if (step == directorySizes.size())
        {
            onDone();
            return;
        }

        directory.clear();
        infra::StringOutputStream{ directory } << "/dir" << directorySizes[step];
        tools.runner.SetPhase(MakePhase("L6,directory,create", directorySizes[step]));
        tools.runner.MakeDirectory(directory, [this](fs::Result)
            {
                count = 0;
                CreateEntry();
            });
    }

    void ReadPatterns::CreateEntry()
    {
        if (count == directorySizes[step])
        {
            List();
            return;
        }

        path = MakePath(directory, count);
        tools.writer.Write({ path, 700 + count, 0, 1 }, [this](bool)
            {
                ++count;
                CreateEntry();
            });
    }

    void ReadPatterns::List()
    {
        tools.runner.SetPhase(MakePhase("L6,list", directorySizes[step]));
        tools.runner.List(directory, [this](fs::Result)
            {
                tools.runner.SetPhase(MakePhase("L6,lookup", directorySizes[step]));
                count = 0;
                LookUp();
            });
    }

    void ReadPatterns::LookUp()
    {
        if (count == lookups)
        {
            ++step;
            CreateDirectory();
            return;
        }

        ++count;
        path = MakePath(directory, random.Below(directorySizes[step]));
        tools.runner.Open(path, fs::OpenMode::read, [this](fs::Result result)
            {
                if (result == fs::Result::success)
                    tools.runner.Close([this](fs::Result)
                        {
                            LookUp();
                        });
                else
                    LookUp();
            });
    }
}
