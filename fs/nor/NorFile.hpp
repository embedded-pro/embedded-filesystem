#ifndef FS_NOR_NOR_FILE_HPP
#define FS_NOR_NOR_FILE_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/interfaces/File.hpp"
#include "fs/nor/NorOpenFile.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    class NorVolume;

    class NorFile
        : public File
        , public PooledHandle
    {
    public:
        explicit NorFile(NorVolume& volume);
        NorFile(const NorFile& other) = delete;
        NorFile& operator=(const NorFile& other) = delete;
        ~NorFile();

        void Open(uint32_t inode, uint32_t size, OpenMode mode);

        uint32_t Size() const override;
        uint32_t Position() const override;
        void Seek(uint32_t position) override;
        void Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone) override;
        void Write(infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone) override;
        void Truncate(const infra::Function<void(Result result)>& onDone) override;
        void ExpectSize(uint32_t size, const infra::Function<void(Result result)>& onDone) override;
        void Sync(const infra::Function<void(Result result)>& onDone) override;
        void Close(const infra::Function<void(Result result)>& onDone) override;

    private:
        void UnReferenced() override;
        void Refuse(const infra::Function<void(Result result)>& onDone);
        void Report(const infra::Function<void(Result result)>& onDone);
        void Granted();
        void ReadDone(Result result, std::size_t bytesRead);

    private:
        NorVolume& volume;
        OperationQueue::Claimer claimer;
        NorOpenFile state;
        uint32_t generation{ 0 };
        bool reading{ false };
        infra::ByteRange readBuffer;
        infra::SharedPtr<NorFile> self;
        infra::AutoResetFunction<void(Result result, std::size_t bytesRead)> onRead;
    };
}

#endif
