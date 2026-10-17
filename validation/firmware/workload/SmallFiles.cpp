#include "validation/firmware/workload/SmallFiles.hpp"

namespace validation
{
    SmallFiles::SmallFiles(WorkloadTools& tools)
        : tools(tools)
    {}

    infra::BoundedConstString SmallFiles::Name() const
    {
        return "L4";
    }

    bool SmallFiles::Fills() const
    {
        return true;
    }

    void SmallFiles::Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone)
    {
        this->onDone = onDone;
        random = metrics::Random{ parameters.seed + 4 };
        index = 0;
        tools.preparation.Prepare(Name(), parameters, window * largestFile, [this]
            {
                Start();
            });
    }

    void SmallFiles::Start()
    {
        tools.runner.SetPhase("L4");
        tools.runner.MakeDirectory("/small", [this](fs::Result)
            {
                WriteFile();
            });
    }

    void SmallFiles::WriteFile()
    {
        if (index == files)
        {
            onDone();
            return;
        }

        auto size = smallestFile + random.Below(largestFile - smallestFile + 1);
        path = MakePath("/small", index);
        tools.writer.Write({ path, 400 + index, size, size }, [this](bool)
            {
                Written();
            });
    }

    void SmallFiles::Written()
    {
        if (index < window)
        {
            ++index;
            WriteFile();
            return;
        }

        path = MakePath("/small", index - window);
        tools.runner.Remove(path, [this](fs::Result)
            {
                ++index;
                WriteFile();
            });
    }
}
