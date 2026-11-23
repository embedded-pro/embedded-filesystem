#pragma once

namespace fs
{
    class NorBackgroundWork
    {
    protected:
        NorBackgroundWork() = default;
        NorBackgroundWork(const NorBackgroundWork& other) = delete;
        NorBackgroundWork& operator=(const NorBackgroundWork& other) = delete;
        ~NorBackgroundWork() = default;

    public:
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual void Urge() = 0;
        virtual void ForegroundClaiming() = 0;
        virtual void ForegroundReleased() = 0;
        virtual bool Scheduled() const = 0;
    };
}
