#include "metrics/report/ReportEnvironment.hpp"
#include <string_view>

namespace metrics
{
    namespace
    {
        constexpr std::string_view outputFlag{ "--metrics-output=" };
    }

    ReportEnvironment::ReportEnvironment(const Report& report, std::string path)
        : report(report)
        , path(std::move(path))
    {}

    void ReportEnvironment::TearDown()
    {
        if (!report.WriteTo(path))
            ADD_FAILURE() << "The metrics report could not be written to " << path;
    }

    std::optional<std::string> TakeMetricsOutputPath(int& argc, char** argv)
    {
        std::optional<std::string> path;
        int kept = 0;

        for (int index = 0; index != argc; ++index)
        {
            std::string_view argument{ argv[index] };
            if (index != 0 && argument.substr(0, outputFlag.size()) == outputFlag)
                path = std::string{ argument.substr(outputFlag.size()) };
            else
                argv[kept++] = argv[index];
        }

        argc = kept;
        return path;
    }
}
