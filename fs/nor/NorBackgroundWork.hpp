#ifndef FS_NOR_NOR_BACKGROUND_WORK_HPP
#define FS_NOR_NOR_BACKGROUND_WORK_HPP

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
    };
}

#endif
