#ifndef VALIDATION_FIRMWARE_CYCLES_CYCLE_SUBJECT_HPP
#define VALIDATION_FIRMWARE_CYCLES_CYCLE_SUBJECT_HPP

#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>

namespace validation
{
    enum class CycleOpenMode : uint8_t
    {
        read,
        write,
        append,
        readWrite
    };

    class CycleSubject
    {
    protected:
        CycleSubject() = default;
        CycleSubject(const CycleSubject& other) = delete;
        CycleSubject& operator=(const CycleSubject& other) = delete;
        ~CycleSubject() = default;

    public:
        static constexpr int32_t success = 0;
        static constexpr int32_t shortTransfer = 100;

        using Done = infra::Function<void(int32_t status)>;

        virtual infra::BoundedConstString Name() const = 0;
        virtual std::size_t RamBytes() const = 0;
        virtual bool Idle() const = 0;

        virtual void Reset(const Done& onDone) = 0;
        virtual void Format(const Done& onDone) = 0;
        virtual void Mount(const Done& onDone) = 0;
        virtual void Unmount(const Done& onDone) = 0;
        virtual void MakeDirectory(infra::BoundedConstString path, const Done& onDone) = 0;
        virtual void Open(infra::BoundedConstString path, CycleOpenMode mode, const Done& onDone) = 0;
        virtual void Seek(uint32_t position) = 0;
        virtual void Read(infra::ByteRange buffer, const Done& onDone) = 0;
        virtual void Write(infra::ConstByteRange data, const Done& onDone) = 0;
        virtual void Sync(const Done& onDone) = 0;
        virtual void Close(const Done& onDone) = 0;
        virtual void List(infra::BoundedConstString path, const Done& onDone) = 0;
        virtual void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const Done& onDone) = 0;
        virtual void Remove(infra::BoundedConstString path, const Done& onDone) = 0;
    };
}

#endif
