#include "validation/firmware/workload/RandomOverwrite.hpp"
#include "metrics/workload/Pattern.hpp"
#include <algorithm>

namespace validation
{
    RandomOverwrite::RandomOverwrite(WorkloadTools& tools)
        : tools(tools)
    {}

    infra::BoundedConstString RandomOverwrite::Name() const
    {
        return "L3";
    }

    bool RandomOverwrite::Fills() const
    {
        return true;
    }

    void RandomOverwrite::Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone)
    {
        this->parameters = parameters;
        this->onDone = onDone;
        random = metrics::Random{ parameters.seed };
        file = 0;
        fileId = 300;
        tools.preparation.Prepare(Name(), parameters, 2 * std::max(Scaled(4 * mebibyte, parameters), overwriteSize), [this]
            {
                Create();
            });
    }

    uint32_t RandomOverwrite::Size() const
    {
        return std::max(Scaled(sizes[file], parameters), overwriteSize);
    }

    void RandomOverwrite::Create()
    {
        if (file == sizes.size())
        {
            onDone();
            return;
        }

        tools.runner.SetPhase(MakePhase("L3,create", Size()));
        tools.writer.Write({ "/overwrite", fileId, Size(), chunkSize }, [this](bool written)
            {
                if (written)
                    Open();
                else
                    onDone();
            });
    }

    void RandomOverwrite::Open()
    {
        tools.runner.SetPhase(MakePhase("L3", Size()));
        tools.runner.Open("/overwrite", fs::OpenMode::readWrite, [this](fs::Result result)
            {
                overwrite = 0;
                if (result == fs::Result::success)
                    Overwrite();
                else
                    onDone();
            });
    }

    void RandomOverwrite::Overwrite()
    {
        if (overwrite == overwrites)
        {
            tools.runner.Close([this](fs::Result)
                {
                    Cleanup();
                });
            return;
        }

        auto offset = random.Below(Size() / overwriteSize) * overwriteSize;
        metrics::FillPattern(infra::MakeRange(data), fileId + 1, offset);
        tools.runner.Seek(offset);
        tools.runner.Write(infra::MakeRange(data), [this](fs::Result)
            {
                tools.runner.Sync([this](fs::Result)
                    {
                        ++overwrite;
                        Overwrite();
                    });
            });
    }

    void RandomOverwrite::Cleanup()
    {
        tools.runner.SetPhase(MakePhase("L3,cleanup", Size()));
        tools.runner.Remove("/overwrite", [this](fs::Result)
            {
                fileId += 2;
                ++file;
                Create();
            });
    }
}
