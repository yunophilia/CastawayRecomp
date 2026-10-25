#include "releases.h"
#include "../kernel/host.h"

#include <cstdio>
#include <iterator>

namespace
{
constexpr KnownFile kFilesR42E69[] = {
#include "files_r42e69.inc"
};

constexpr uint64_t TitleId(const char* id)
{
    return 0x00010000'00000000ull | ((uint64_t)(uint8_t)id[0] << 24) | ((uint64_t)(uint8_t)id[1] << 16) |
           ((uint64_t)(uint8_t)id[2] << 8) | (uint8_t)id[3];
}
}   // namespace

const GameRelease kReleases[1] = {
    { "R42E69", "USA", TitleId("R42E"), "USA", "RVL-001(USA)", "LU", "NTSC", "US", 0x31,
      kFilesR42E69, std::size(kFilesR42E69) },
};

const char* const kReleaseNames = "USA";

const GameRelease* Release_Find(const std::string& gameId)
{
    for (auto& r : kReleases)
        if (gameId == r.gameId) return &r;
    return nullptr;
}

std::string Release_InstalledId(const std::filesystem::path& data)
{
    char id[6] = {};
    FILE* f = Host_Open((data / "sys" / "boot.bin").c_str(), "rb");
    if (!f) return {};
    size_t n = fread(id, 1, 6, f);
    fclose(f);
    return n == 6 ? std::string(id, 6) : std::string();
}
