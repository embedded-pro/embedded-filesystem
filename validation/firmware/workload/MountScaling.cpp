#include "validation/firmware/workload/MountScaling.hpp"

namespace validation
{
    MountScaling::MountScaling(WorkloadTools& tools)
        : tools(tools)
    {}

    infra::BoundedConstString MountScaling::Name() const
    {
        return "L7";
    }

    bool MountScaling::Fills() const
    {
        return false;
    }

    void MountScaling::Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone)
    {
        this->parameters = parameters;
        this->onDone = onDone;
        level = 0;
        Prepare();
    }

    void MountScaling::Prepare()
    {
        if (level == fillLevels.size())
        {
            onDone();
            return;
        }

        tools.runner.SetPhase(MakePhase("L7,prepare", fillLevels[level]));
        tools.runner.Format([this](fs::Result)
            {
                tools.runner.Mount([this](fs::Result)
                    {
                        tools.filler.Fill(fillLevels[level], parameters, 0, [this]
                            {
                                tools.runner.Unmount([this](fs::Result)
                                    {
                                        Mount();
                                    });
                            });
                    });
            });
    }

    void MountScaling::Mount()
    {
        tools.runner.SetPhase(MakePhase("L7", fillLevels[level]));
        tools.runner.Mount([this](fs::Result)
            {
                tools.runner.SetPhase(MakePhase("L7,cleanup", fillLevels[level]));
                tools.runner.Unmount([this](fs::Result)
                    {
                        ++level;
                        Prepare();
                    });
            });
    }
}
