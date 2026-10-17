#include "validation/firmware/workload/SequentialWrite.hpp"

namespace validation
{
    SequentialWrite::SequentialWrite(WorkloadTools& tools)
        : tools(tools)
    {}

    infra::BoundedConstString SequentialWrite::Name() const
    {
        return "L1";
    }

    bool SequentialWrite::Fills() const
    {
        return false;
    }

    void SequentialWrite::Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone)
    {
        this->parameters = parameters;
        this->parameters.fillPercent = 0;
        this->onDone = onDone;
        file = 0;
        tools.preparation.Prepare(Name(), this->parameters, 0, [this]
            {
                WriteFile();
            });
    }

    void SequentialWrite::WriteFile()
    {
        if (file == sizes.size() * chunks.size())
        {
            onDone();
            return;
        }

        auto size = Scaled(sizes[file / chunks.size()], parameters);
        auto chunk = chunks[file % chunks.size()];
        tools.runner.SetPhase(MakePhase("L1", size, chunk));
        tools.writer.Write({ "/sequential", static_cast<uint32_t>(100 + file), size, chunk, true }, [this](bool)
            {
                tools.runner.Settle(settleLimit, [this]
                    {
                        Cleanup();
                    });
            });
    }

    void SequentialWrite::Cleanup()
    {
        tools.runner.SetPhase(MakePhase("L1,cleanup", Scaled(sizes[file / chunks.size()], parameters), chunks[file % chunks.size()]));
        tools.runner.Remove("/sequential", [this](fs::Result)
            {
                tools.runner.Settle(settleLimit, [this]
                    {
                        ++file;
                        WriteFile();
                    });
            });
    }
}
