// Scripted test input shared by the platform layers (desktop: environment variables,
// Wii U: "autotest.txt" next to the content with KEY=VALUE lines). Plain C++ only.
#pragma once
#include "platform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>

namespace plat {
const char* autotest_config(const char* key);   // implemented per platform
int& autotest_frame_counter();

namespace autotest {
inline int& g_frame_ref() { return autotest_frame_counter(); }
#define g_frame (::plat::autotest::g_frame_ref())
#define getenv(k) ::plat::autotest_config(k)
// test hook: TF_VPADS=n makes pads 0..n-1 virtual; TF_PADKEYS="frame:pad:buttonbits:frames[:lx:ly];..."
inline int VirtualPads() { static int n = getenv("TF_VPADS") ? atoi(getenv("TF_VPADS")) : 0; return n; }
inline PadState VirtualPad(int index)
{
    static std::vector<std::array<float, 6>> script;
    static bool parsed = false;
    if (!parsed)
    {
        parsed = true;
        if (const char* e = getenv("TF_PADKEYS"))
        {
            std::string s = e;
            size_t p = 0;
            while (p < s.size())
            {
                size_t q = s.find(';', p);
                std::string item = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
                float f = 0, pd = 0, bits = 0, d = 3, lx = 0, ly = 0;
                if (sscanf(item.c_str(), "%f:%f:%f:%f:%f:%f", &f, &pd, &bits, &d, &lx, &ly) >= 3) script.push_back({ f, pd, bits, d, lx, ly });
                if (q == std::string::npos) break;
                p = q + 1;
            }
        }
    }
    PadState s;
    s.connected = true;
    static int randomAt = getenv("TF_RANDOM_AT") ? atoi(getenv("TF_RANDOM_AT")) : -1;
    if (randomAt >= 0 && g_frame >= randomAt)
    {
        // deterministic pseudo-random play: hold inputs for ~10 frames
        uint32_t h = (uint32_t)(g_frame / 10) * 2654435761u ^ (uint32_t)(index + 1) * 40503u;
        h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
        const uint32_t pool[] = { 4096, 16384, 512, 32768, 4096 | 16384, 0, 8192 };
        s.buttons = pool[h % 7];
        // never press Start/Back during random play
        s.lx = (float)((int)((h >> 8) % 3) - 1);
        s.ly = (float)((int)((h >> 12) % 3) - 1);
        return s;
    }
    for (auto& it : script)
        if ((int)it[1] == index && g_frame >= (int)it[0] && g_frame < (int)(it[0] + it[3]))
        {
            s.buttons |= (uint32_t)it[2];
            if (it[4] != 0) s.lx = it[4];
            if (it[5] != 0) s.ly = it[5];
        }
    return s;
}

// test hook: TF_KEYS="frame:xnaKey:frames;..." injects key presses
inline bool ScriptedKey(int k)
{
    static std::vector<std::array<int, 3>> script;
    static bool parsed = false;
    if (!parsed)
    {
        parsed = true;
        if (const char* e = getenv("TF_KEYS"))
        {
            std::string s = e;
            size_t p = 0;
            while (p < s.size())
            {
                size_t q = s.find(';', p);
                std::string item = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
                int f = 0, key = 0, d = 3;
                if (sscanf(item.c_str(), "%d:%d:%d", &f, &key, &d) >= 2) script.push_back({ f, key, d });
                if (q == std::string::npos) break;
                p = q + 1;
            }
        }
    }
    for (auto& it : script) if (it[1] == k && g_frame >= it[0] && g_frame < it[0] + it[2]) return true;
    return false;
}
#undef getenv
#undef g_frame
} // namespace autotest
} // namespace plat
