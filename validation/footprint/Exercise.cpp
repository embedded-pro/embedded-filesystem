#include "validation/footprint/Exercise.hpp"
#include <optional>

namespace validation::footprint
{
    namespace
    {
        fs::FileSystem* volume = nullptr;
        infra::SharedPtr<fs::File> file;
        infra::SharedPtr<fs::Directory> directory;
        infra::ByteRange buffer;

        void ManageEntries()
        {
            volume->Stat("/log.txt", [](fs::Result, const fs::EntryInfo&)
                {
                    volume->Rename("/log.txt", "/old.txt", [](fs::Result)
                        {
                            volume->Remove("/old.txt", [](fs::Result)
                                {
                                    volume->MakeDirectory("/data", [](fs::Result)
                                        {
                                            volume->Usage([](fs::Result, fs::VolumeUsage)
                                                {
                                                    volume->Unmount([](fs::Result) {});
                                                });
                                        });
                                });
                        });
                });
        }

        void List()
        {
            volume->OpenDirectory("/", [](fs::Result, infra::SharedPtr<fs::Directory> opened)
                {
                    directory = opened;
                    directory->Next([](fs::Result, std::optional<fs::EntryInfo>)
                        {
                            directory = nullptr;
                            ManageEntries();
                        });
                });
        }

        void ListAndStat()
        {
            volume->OpenDirectory("/", [](fs::Result, infra::SharedPtr<fs::Directory> opened)
                {
                    directory = opened;
                    directory->Next([](fs::Result, std::optional<fs::EntryInfo>)
                        {
                            directory = nullptr;
                            volume->Stat("/log.txt", [](fs::Result, const fs::EntryInfo&)
                                {
                                    volume->Unmount([](fs::Result) {});
                                });
                        });
                });
        }

        void Read()
        {
            volume->Open("/log.txt", fs::OpenMode::read, [](fs::Result, infra::SharedPtr<fs::File> opened)
                {
                    file = opened;
                    file->Seek(0);
                    file->Read(buffer, [](fs::Result, std::size_t)
                        {
                            file->Close([](fs::Result)
                                {
                                    file = nullptr;
                                    ListAndStat();
                                });
                        });
                });
        }

        void ReadAndWrite()
        {
            volume->Open("/log.txt", fs::OpenMode::readWrite, [](fs::Result, infra::SharedPtr<fs::File> opened)
                {
                    file = opened;
                    file->ExpectSize(4096, [](fs::Result)
                        {
                            file->Write(buffer, [](fs::Result)
                                {
                                    file->Seek(file->Size() - file->Position());
                                    file->Read(buffer, [](fs::Result, std::size_t)
                                        {
                                            file->Truncate([](fs::Result)
                                                {
                                                    file->Sync([](fs::Result)
                                                        {
                                                            file->Close([](fs::Result)
                                                                {
                                                                    file = nullptr;
                                                                    List();
                                                                });
                                                        });
                                                });
                                        });
                                });
                        });
                });
        }
    }

    void Exercise(fs::FileSystem& fileSystem, infra::ByteRange ioBuffer)
    {
        volume = &fileSystem;
        buffer = ioBuffer;
        fileSystem.Mount([](fs::Result)
            {
                ReadAndWrite();
            });
    }

    void ExerciseReads(fs::FileSystem& fileSystem, infra::ByteRange ioBuffer)
    {
        volume = &fileSystem;
        buffer = ioBuffer;
        fileSystem.Mount([](fs::Result)
            {
                Read();
            });
    }
}
