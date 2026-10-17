#include "validation/firmware/workload/LogAppender.hpp"
#include "metrics/workload/Pattern.hpp"

namespace validation
{
    LogAppender::LogAppender(WorkloadRunner& runner)
        : runner(runner)
    {}

    LogAppender::~LogAppender() = default;

    void LogAppender::Append(infra::BoundedConstString path, uint32_t fileId, uint32_t size, const infra::Function<void()>& onDone)
    {
        this->fileId = fileId;
        this->size = size;
        this->onDone = onDone;
        offset = 0;
        runner.Open(path, fs::OpenMode::append, [this](fs::Result result)
            {
                if (result == fs::Result::success)
                    AppendNext();
                else
                    this->onDone();
            });
    }

    void LogAppender::AppendNext()
    {
        if (offset + appendSize > size)
        {
            Close();
            return;
        }

        metrics::FillPattern(infra::MakeRange(record), fileId, offset);
        runner.Write(infra::MakeRange(record), [this](fs::Result result)
            {
                Appended(result);
            });
    }

    void LogAppender::Appended(fs::Result result)
    {
        if (result != fs::Result::success)
        {
            Close();
            return;
        }

        runner.Sync([this](fs::Result)
            {
                runner.Idle(idlePeriod, [this]
                    {
                        offset += appendSize;
                        AppendNext();
                    });
            });
    }

    void LogAppender::Close()
    {
        runner.Close([this](fs::Result)
            {
                onDone();
            });
    }
}
