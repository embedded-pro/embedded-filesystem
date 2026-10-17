#include "validation/firmware/workload/DataLogger.hpp"
#include <algorithm>

namespace validation
{
    DataLogger::DataLogger(WorkloadTools& tools)
        : tools(tools)
        , appender(tools.runner)
    {}

    infra::BoundedConstString DataLogger::Name() const
    {
        return "L2";
    }

    bool DataLogger::Fills() const
    {
        return true;
    }

    void DataLogger::Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone)
    {
        this->onDone = onDone;
        size = Scaled(4 * mebibyte, parameters);
        secondSize = std::min(size, secondLogLimit);
        tools.preparation.Prepare(Name(), parameters, uint64_t{ size } + secondSize, [this]
            {
                FirstLog();
            });
    }

    void DataLogger::FirstLog()
    {
        tools.runner.SetPhase("L2", PhaseWindow::firstLog);
        appender.Append("/log", 200, size, [this]
            {
                SecondLog();
            });
    }

    void DataLogger::SecondLog()
    {
        tools.runner.SetPhase("L2,second,log", PhaseWindow::secondLog);
        appender.Append("/log2", 201, secondSize, [this]
            {
                onDone();
            });
    }
}
