#include "infra/util/StaticStorage.hpp"
#include "lfs.h"
#include "validation/firmware/baselines/LittleFsFlash.hpp"
#include "validation/footprint/Platform.hpp"
#include <array>
#include <cstdint>

namespace
{
    constexpr auto configuration = validation::footprint::littleFsDefault;
}

int main()
{
    static validation::footprint::Platform platform;
    static infra::StaticStorage<validation::LittleFsFlash> flash;
    flash.Construct(platform.flash);
    static std::array<uint8_t, configuration.cacheSize> readBuffer;
    static std::array<uint8_t, configuration.cacheSize> programBuffer;
    static std::array<uint8_t, configuration.lookaheadSize> lookaheadBuffer;
    static std::array<uint8_t, configuration.cacheSize> fileBuffer;
    static lfs_config config{};
    static lfs_t lfs;
    static lfs_file_t file;
    static lfs_file_config fileConfig{};
    auto& buffer = platform.buffer;

    flash->Configure(config);
    config.read_size = configuration.readSize;
    config.prog_size = configuration.programSize;
    config.block_cycles = configuration.blockCycles;
    config.cache_size = configuration.cacheSize;
    config.lookahead_size = configuration.lookaheadSize;
    config.read_buffer = readBuffer.data();
    config.prog_buffer = programBuffer.data();
    config.lookahead_buffer = lookaheadBuffer.data();
    fileConfig.buffer = fileBuffer.data();

    lfs_mount(&lfs, &config);
    lfs_file_opencfg(&lfs, &file, "log.txt", LFS_O_RDONLY, &fileConfig);
    lfs_file_seek(&lfs, &file, 0, LFS_SEEK_SET);
    lfs_file_read(&lfs, &file, buffer.data(), buffer.size());
    lfs_file_close(&lfs, &file);

    lfs_dir_t directory;
    lfs_info information;
    lfs_dir_open(&lfs, &directory, "/");
    lfs_dir_read(&lfs, &directory, &information);
    lfs_dir_close(&lfs, &directory);
    lfs_stat(&lfs, "log.txt", &information);
    lfs_unmount(&lfs);

    platform.Run();
}
