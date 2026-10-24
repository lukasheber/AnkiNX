// Host benchmark for the forest (app/forest): builds each sample scenery,
// draws frames, reports the time each step takes and writes a PNG of each.
//
//   forest_bench forest.js samples/ out/ [width height frames [preset...]]
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <zlib.h>

#include "forest/forest_script.hpp"

using Clock = std::chrono::steady_clock;

namespace
{

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

double ms(Clock::time_point since) { return std::chrono::duration<double, std::milli>(Clock::now() - since).count(); }

void put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int i = 3; i >= 0; i--)
        out.push_back((uint8_t)(v >> (i * 8)));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& body)
{
    put32(out, (uint32_t)body.size());
    size_t at = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    put32(out, (uint32_t)crc32(0, out.data() + at, (uInt)(out.size() - at)));
}

// Premultiplied RGBA, scaled up `k` times, over black.
void writePng(const std::string& path, const uint8_t* px, int w, int h, int k)
{
    int W = w * k, H = h * k;
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(W * 3 + 1) * H);
    for (int y = 0; y < H; y++)
    {
        raw.push_back(0);
        for (int x = 0; x < W; x++)
        {
            const uint8_t* p = px + ((size_t)(y / k) * w + x / k) * 4;
            raw.insert(raw.end(), { p[0], p[1], p[2] });
        }
    }
    uLongf size = compressBound(raw.size());
    std::vector<uint8_t> z(size);
    compress2(z.data(), &size, raw.data(), raw.size(), 6);
    z.resize(size);
    std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    std::vector<uint8_t> ihdr;
    put32(ihdr, W);
    put32(ihdr, H);
    ihdr.insert(ihdr.end(), { 8, 2, 0, 0, 0 });
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    std::ofstream(path, std::ios::binary).write((const char*)out.data(), out.size());
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: forest_bench forest.js samples/ out/ [width height frames [preset...]]\n");
        return 2;
    }
    std::string bundle = readFile(argv[1]), samples = argv[2], out = argv[3];
    int width = argc > 4 ? std::atoi(argv[4]) : 320, height = argc > 5 ? std::atoi(argv[5]) : 180;
    int frames = argc > 6 ? std::atoi(argv[6]) : 60;
    std::vector<std::string> presets;
    for (int i = 7; i < argc; i++)
        presets.push_back(argv[i]);
    if (presets.empty())
        presets = { "golden_lake", "misty_valley", "starry_night", "aurora", "lanterns",
                    "frozen_market", "pumpkin_patch", "bamboo", "synthwave" };

    int failed = 0;
    std::printf("%-14s %8s %8s %9s %9s %9s %7s\n", "preset", "load ms", "build ms", "frame avg", "frame max", "busy",
                "JS MB");
    for (const std::string& name : presets)
    {
        std::string data = readFile(samples + "/" + name + ".json");
        if (data.empty())
        {
            std::printf("%-14s no sample\n", name.c_str());
            failed++;
            continue;
        }
        auto t0 = Clock::now();
        ForestScript forest(bundle);
        double load = ms(t0);
        t0          = Clock::now();
        if (!forest.build(data, width, height))
        {
            std::printf("%-14s build failed: %s\n", name.c_str(), forest.error().c_str());
            failed++;
            continue;
        }
        double build = ms(t0), total = 0, worst = 0;
        bool ok = true;
        for (int i = 0; i < frames && ok; i++)
        {
            t0 = Clock::now();
            ok = forest.frame(10.0 + i / 15.0);
            double one = ms(t0);
            total += one;
            worst = std::max(worst, one);
        }
        if (!ok)
        {
            std::printf("%-14s frame failed: %s\n", name.c_str(), forest.error().c_str());
            failed++;
            continue;
        }
        bool busy = forest.busy();
        std::printf("%-14s %8.0f %8.0f %9.1f %9.1f %9s %7.1f\n", name.c_str(), load, build, total / frames, worst,
                    busy ? "yes" : "no", forest.memoryUsed() / 1048576.0);
        writePng(out + "/" + name + ".png", forest.pixels(), forest.width(), forest.height(), 3);
    }
    return failed ? 1 : 0;
}
