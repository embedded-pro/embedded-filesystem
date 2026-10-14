#ifndef FS_NOR_NOR_FILE_HPP
#define FS_NOR_NOR_FILE_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/interfaces/File.hpp"
#include "fs/nor/NorOpenFile.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/IntrusiveList.hpp"
#include "infra/util/SharedPtr.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace fs
{
    class NorVolume;

    class NorFile
        : public File
        , public PooledHandle
        , public infra::IntrusiveList<NorFile>::NodeType
    {
    public:
        template<std::size_t BufferBytes>
        class WithBuffer;

        NorFile(NorVolume& volume, infra::ByteRange buffer);
        NorFile(const NorFile& other) = delete;
        NorFile& operator=(const NorFile& other) = delete;
        ~NorFile();

        void Open(const NorOpenFile& opened);
        infra::ByteRange Buffer() const;
        NorOpenFile& State();
        const NorOpenFile& State() const;
        void Detach();

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
        enum class Call : uint8_t
        {
            none,
            read,
            write,
            sync,
            close,
            implicitClose
        };

        void UnReferenced() override;
        bool Pending() const;
        Result Unwritable() const;
        Result Availability() const;
        void Claim(Call call);
        void Granted();
        void ReadDone(Result result, std::size_t bytesRead);
        void Done(Result result);
        void ReleaseUncommittedBlocks();
        void Forget();

    private:
        NorVolume& volume;
        infra::ByteRange buffer;
        OperationQueue::Claimer claimer;
        NorOpenFile state;
        uint32_t generation{ 0 };
        bool open{ false };
        Call call{ Call::none };
        infra::ByteRange readBuffer;
        infra::ConstByteRange writeData;
        infra::SharedPtr<NorFile> self;
        infra::AutoResetFunction<void(Result result, std::size_t bytesRead)> onRead;
        infra::AutoResetFunction<void(Result result)> onDone;
    };

    namespace detail
    {
        template<std::size_t BufferBytes>
        struct NorFileBuffer
        {
            std::array<uint8_t, BufferBytes> bufferStorage{};
        };
    }

    template<std::size_t BufferBytes>
    class NorFile::WithBuffer
        : private detail::NorFileBuffer<BufferBytes>
        , public NorFile
    {
    public:
        explicit WithBuffer(NorVolume& volume)
            : NorFile(volume, infra::MakeRange(this->bufferStorage))
        {}
    };
}

#endif
