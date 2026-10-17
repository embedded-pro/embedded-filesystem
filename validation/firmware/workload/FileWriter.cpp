#include "validation/firmware/workload/FileWriter.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "metrics/workload/Pattern.hpp"
#include <algorithm>

namespace validation
{
    FileWriter::FileWriter(WorkloadRunner& runner)
        : runner(runner)
    {}

    FileWriter::~FileWriter() = default;

    void FileWriter::Write(const FileToWrite& file, const infra::Function<void(bool written)>& onDone)
    {
        really_assert(file.chunk != 0 && file.chunk <= maximumChunk);

        path = file.path;
        this->file = file;
        this->file.path = path;
        this->onDone = onDone;
        offset = 0;
        runner.Open(path, fs::OpenMode::write, [this](fs::Result result)
            {
                Opened(result);
            });
    }

    void FileWriter::Opened(fs::Result result)
    {
        if (result != fs::Result::success)
            onDone(false);
        else if (file.announceSize)
            runner.ExpectSize(file.size, [this](fs::Result)
                {
                    WriteNext();
                });
        else
            WriteNext();
    }

    void FileWriter::WriteNext()
    {
        if (offset >= file.size)
        {
            Finish();
            return;
        }

        auto part = infra::Head(infra::MakeRange(data), std::min(file.chunk, file.size - offset));
        metrics::FillPattern(part, file.fileId, offset);
        runner.Write(part, [this](fs::Result result)
            {
                Written(result);
            });
    }

    void FileWriter::Written(fs::Result result)
    {
        if (result != fs::Result::success)
        {
            runner.Close([this](fs::Result)
                {
                    onDone(false);
                });
            return;
        }

        offset += file.chunk;
        WriteNext();
    }

    void FileWriter::Finish()
    {
        runner.Sync([this](fs::Result)
            {
                runner.Close([this](fs::Result result)
                    {
                        onDone(result == fs::Result::success);
                    });
            });
    }
}
