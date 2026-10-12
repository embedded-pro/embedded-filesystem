#ifndef FS_INTERFACES_FILE_HPP
#define FS_INTERFACES_FILE_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    class File
    {
    protected:
        File() = default;
        File(const File& other) = delete;
        File& operator=(const File& other) = delete;
        ~File() = default;

    public:
        virtual uint32_t Size() const = 0;
        virtual uint32_t Position() const = 0;
        virtual void Seek(uint32_t position) = 0;

        virtual void Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone) = 0;
        virtual void Write(infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Truncate(const infra::Function<void(Result result)>& onDone) = 0;
        virtual void ExpectSize(uint32_t size, const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Sync(const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Close(const infra::Function<void(Result result)>& onDone) = 0;
    };
}

#endif
