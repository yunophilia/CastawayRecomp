// Extracts a Wii disc image (ISO/RVZ/WIA/WBFS) into sys/ and files/ using nod.
// usage: wii_extract <image> <dest dir>
#include <nod.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct FstEntry
{
    uint32_t index;
    bool dir;
    uint32_t size;   // file size, or the end index for directories
    std::string name;
};

static uint32_t CollectFst(uint32_t index, NodNodeKind kind, const char* name, uint32_t size, void* user)
{
    ((std::vector<FstEntry>*)user)->push_back({ index, kind == NOD_NODE_KIND_DIRECTORY, size, name });
    return index + 1;
}

static bool ReadExact(NodHandle* h, uint8_t* out, size_t len)
{
    while (len)
    {
        int64_t n = nod_read(h, out, len);
        if (n <= 0) return false;
        out += n;
        len -= (size_t)n;
    }
    return true;
}

static bool WriteFile(const fs::path& p, const void* data, size_t len)
{
    FILE* f = _wfopen(p.c_str(), L"wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static int Fail(const char* what)
{
    const char* m = nod_error_message();
    fprintf(stderr, "%s: %s\n", what, m ? m : "unknown error");
    return 1;
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: wii_extract <image> <dest dir>\n");
        return 2;
    }
    NodDiscOptions opt{};
    opt.preloader_threads = 1;
    NodHandle* disc = nullptr;
    if (nod_disc_open(argv[1], &opt, &disc) != NOD_RESULT_OK) return Fail("open");
    NodDiscHeader header{};
    if (nod_disc_header(disc, &header) != NOD_RESULT_OK) return Fail("header");
    printf("game id %.6s\n", header.game_id);

    NodHandle* part = nullptr;
    if (nod_disc_open_partition_kind(disc, NOD_PARTITION_KIND_DATA, nullptr, &part) != NOD_RESULT_OK)
        return Fail("data partition");
    NodPartitionMeta meta{};
    if (nod_partition_meta(part, &meta) != NOD_RESULT_OK) return Fail("partition meta");

    fs::path dest = argv[2];
    std::error_code ec;
    fs::create_directories(dest / "sys", ec);
    fs::create_directories(dest / "files", ec);
    struct Blob { const char* path; NodBlob blob; };
    const Blob blobs[] = {
        { "sys/boot.bin", meta.raw_boot }, { "sys/bi2.bin", meta.raw_bi2 },
        { "sys/apploader.img", meta.raw_apploader }, { "sys/main.dol", meta.raw_dol },
        { "sys/fst.bin", meta.raw_fst },
    };
    for (auto& b : blobs)
        if (b.blob.data && !WriteFile(dest / b.path, b.blob.data, b.blob.size)) return Fail(b.path);

    std::vector<FstEntry> fst;
    nod_partition_iterate_fst(part, CollectFst, &fst);
    // directories carry the index one past their last child
    std::vector<std::pair<uint32_t, fs::path>> dirs = { { UINT32_MAX, dest / "files" } };
    std::vector<uint8_t> buf(1 << 20);
    uint64_t files = 0, bytes = 0;
    for (auto& e : fst)
    {
        while (dirs.size() > 1 && e.index >= dirs.back().first) dirs.pop_back();
        fs::path p = dirs.back().second / fs::path(std::u8string(e.name.begin(), e.name.end()));
        if (e.dir)
        {
            if (e.index == 0) continue;   // root
            fs::create_directories(p, ec);
            dirs.push_back({ e.size, p });
            continue;
        }
        NodHandle* file = nullptr;
        if (nod_partition_open_file(part, e.index, &file) != NOD_RESULT_OK) return Fail(e.name.c_str());
        FILE* out = _wfopen(p.c_str(), L"wb");
        if (!out) return Fail(p.string().c_str());
        for (uint64_t left = e.size; left;)
        {
            size_t n = (size_t)std::min<uint64_t>(left, buf.size());
            if (!ReadExact(file, buf.data(), n) || fwrite(buf.data(), 1, n, out) != n)
            {
                fclose(out);
                return Fail(e.name.c_str());
            }
            left -= n;
        }
        fclose(out);
        nod_free(file);
        files++;
        bytes += e.size;
    }
    printf("%llu files, %llu bytes\n", (unsigned long long)files, (unsigned long long)bytes);
    nod_free(part);
    nod_free(disc);
    return 0;
}
