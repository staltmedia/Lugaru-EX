/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "EX/ExProfile.hpp"

#include "Graphic/gamegl.hpp"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif

namespace EX
{
typedef void(APIENTRY* PFNGENQUERIES)(GLsizei, GLuint*);
typedef void(APIENTRY* PFNQUERYCOUNTER)(GLuint, GLenum);
typedef void(APIENTRY* PFNGETQUERYOBJECTUI64V)(GLuint, GLenum, unsigned long long*);
typedef void(APIENTRY* PFNGETQUERYOBJECTIV)(GLuint, GLenum, GLint*);
static PFNGENQUERIES p_glGenQueries;
static PFNQUERYCOUNTER p_glQueryCounter;
static PFNGETQUERYOBJECTUI64V p_glGetQueryObjectui64v;
static PFNGETQUERYOBJECTIV p_glGetQueryObjectiv;

static const int kMaxMarks = 192; // per frame
static const int kRing = 6;      // frames of GPU queries in flight (read 5 frames late)

struct FrameQ
{
    GLuint q[kMaxMarks + 1];
    int part[kMaxMarks + 1]; // which part each query ends (-1 = the frame start)
    int n = 0;
    bool used = false;
};

static bool g_on = false, g_init = false, g_gpu = false;
static std::vector<std::string> g_names;
static std::vector<double> g_cpu, g_gpuMs;
static std::vector<long> g_gpuFrames;
static FrameQ g_ring[kRing];
static int g_cur = 0;
static Uint64 g_last = 0, g_frameStart = 0;
static double g_frameCpu = 0;
static Uint64 g_reportFrom = 0; // wall clock of the last report
static long g_frames = 0, g_harvested = 0; // frames timed / frames whose GPU times came back

static bool g_pending = false; // switch on at the next frame start (ProfAuto)

void ProfAuto(const char* why)
{
    if (!g_on && !g_pending) {
        g_pending = true;
        printf("LugaruEX profile: switching on (%s) - the log gets a frame-time breakdown every 5 seconds\n", why);
    }
}

// frameStart: only a frame's start may switch it on (the marks of a frame belong together)
static bool turnOn(bool frameStart)
{
    if (!g_init || (frameStart && g_pending && !g_on)) {
        g_init = true;
        g_on = getenv("LUGARUEX_PROFILE") != nullptr || (frameStart && g_pending);
        if (g_on) {
            g_pending = false;
            p_glGenQueries = (PFNGENQUERIES)SDL_GL_GetProcAddress("glGenQueries");
            p_glQueryCounter = (PFNQUERYCOUNTER)SDL_GL_GetProcAddress("glQueryCounter");
            p_glGetQueryObjectui64v = (PFNGETQUERYOBJECTUI64V)SDL_GL_GetProcAddress("glGetQueryObjectui64v");
            p_glGetQueryObjectiv = (PFNGETQUERYOBJECTIV)SDL_GL_GetProcAddress("glGetQueryObjectiv");
            g_gpu = p_glGenQueries && p_glQueryCounter && p_glGetQueryObjectui64v && p_glGetQueryObjectiv;
            if (g_gpu) {
                for (FrameQ& f : g_ring) {
                    p_glGenQueries(kMaxMarks + 1, f.q);
                }
            }
            printf("LugaruEX profile: on (%s)\n", g_gpu ? "CPU and GPU times" : "CPU times only");
        }
    }
    return g_on;
}

bool ProfOn()
{
    return g_init ? g_on : turnOn(false);
}

static int partIndex(const char* name)
{
    for (size_t i = 0; i < g_names.size(); i++) {
        if (g_names[i] == name) {
            return (int)i;
        }
    }
    g_names.push_back(name);
    g_cpu.push_back(0);
    g_gpuMs.push_back(0);
    g_gpuFrames.push_back(0);
    return (int)g_names.size() - 1;
}

static void stamp(int part)
{
    FrameQ& f = g_ring[g_cur];
    if (g_gpu && f.n < kMaxMarks + 1) {
        p_glQueryCounter(f.q[f.n], GL_TIMESTAMP);
        f.part[f.n] = part;
        f.n++;
    }
}

// the queries of the frame about to be reused: add their GPU times
static void harvest(FrameQ& f)
{
    if (!f.used || f.n < 2) {
        f.n = 0;
        return;
    }
    GLint ok = 0;
    p_glGetQueryObjectiv(f.q[f.n - 1], GL_QUERY_RESULT_AVAILABLE, &ok);
    if (ok) {
        unsigned long long prev = 0, t = 0;
        p_glGetQueryObjectui64v(f.q[0], GL_QUERY_RESULT, &prev);
        for (int i = 1; i < f.n; i++) {
            p_glGetQueryObjectui64v(f.q[i], GL_QUERY_RESULT, &t);
            if (f.part[i] >= 0 && t >= prev) {
                g_gpuMs[f.part[i]] += (t - prev) / 1e6;
            }
            prev = t;
        }
        g_harvested++;
    }
    f.n = 0;
}

void ProfFrameStart()
{
    if (!turnOn(true)) {
        return;
    }
    g_cur = (g_cur + 1) % kRing;
    if (g_gpu) {
        harvest(g_ring[g_cur]);
    }
    g_ring[g_cur].used = true;
    g_frameStart = g_last = SDL_GetPerformanceCounter();
    stamp(-1);
}

void ProfMark(const char* name)
{
    const Uint64 now = SDL_GetPerformanceCounter();
    const int i = partIndex(name);
    g_cpu[i] += (double)(now - g_last) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    g_last = now;
    stamp(i);
}

void ProfFrameEnd()
{
    if (!ProfOn()) {
        return;
    }
    ProfMark("swap");
    const double ms = (double)(g_last - g_frameStart) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    g_frameCpu += ms;
    if (!g_reportFrom) {
        g_reportFrom = g_last;
    }
    g_frames++;
    if ((double)(g_last - g_reportFrom) / (double)SDL_GetPerformanceFrequency() < 5.0 || g_frames == 0) {
        return;
    }
    char b[96];
    const double secs = (double)(g_last - g_reportFrom) / (double)SDL_GetPerformanceFrequency();
    snprintf(b, sizeof(b), "%.0f fps, work %.2f ms per frame", g_frames / secs, g_frameCpu / g_frames);
    std::string line = std::string("LugaruEX profile (ms per frame, CPU / GPU): ") + b;
    for (size_t i = 0; i < g_names.size(); i++) {
        const double c = g_cpu[i] / g_frames;
        const double gp = g_harvested ? g_gpuMs[i] / g_harvested : 0;
        snprintf(b, sizeof(b), " | %s %.2f/%.2f", g_names[i].c_str(), c, gp);
        line += b;
        g_cpu[i] = 0;
        g_gpuMs[i] = 0;
        g_gpuFrames[i] = 0;
    }
    printf("%s\n", line.c_str());
    fflush(stdout);
    g_frameCpu = 0;
    g_reportFrom = g_last;
    g_frames = 0;
    g_harvested = 0;
}
}

// Test-only sampling profiler (LUGARUEX_TEST_SAMPLE=<folder with the PDB>): a thread looks at
// where the main thread is 2000 times a second; SamplerReport prints the busiest functions.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <mmsystem.h>
#include <algorithm>
#include <map>
#include <thread>
#include <atomic>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")

namespace EX
{
static std::map<unsigned long long, long> g_samples;
static std::atomic<bool> g_sampling(false);
static long g_sampleCount = 0;
static HANDLE g_mainThread = nullptr;

void SamplerStart()
{
    if (!getenv("LUGARUEX_TEST_SAMPLE") || g_sampling) {
        return;
    }
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    g_sampling = true;
    std::thread([] {
        timeBeginPeriod(1);
        while (g_sampling) {
            if (SuspendThread(g_mainThread) != (DWORD)-1) {
                CONTEXT c;
                c.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(g_mainThread, &c)) {
                    g_samples[c.Rip]++;
                    g_sampleCount++;
                }
                ResumeThread(g_mainThread);
            }
            Sleep(0);
            const DWORD until = GetTickCount() + 1;
            while (GetTickCount() < until && g_sampling) {
                Sleep(0);
            }
        }
    }).detach();
}

void SamplerReport()
{
    if (!g_sampling) {
        return;
    }
    g_sampling = false;
    Sleep(20);
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, getenv("LUGARUEX_TEST_SAMPLE"), TRUE);
    std::map<std::string, long> byFunc;
    char buf[sizeof(SYMBOL_INFO) + 256];
    for (auto& s : g_samples) {
        SYMBOL_INFO* si = (SYMBOL_INFO*)buf;
        memset(buf, 0, sizeof(buf));
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 255;
        DWORD64 disp = 0;
        std::string name;
        IMAGEHLP_MODULE64 mi;
        memset(&mi, 0, sizeof(mi));
        mi.SizeOfStruct = sizeof(mi);
        const bool mod = SymGetModuleInfo64(proc, s.first, &mi) != 0;
        if (SymFromAddr(proc, s.first, &disp, si)) {
            name = si->Name;
        } else {
            name = "?";
        }
        if (mod) {
            name = std::string(mi.ModuleName) + "!" + name;
        }
        byFunc[name] += s.second;
    }
    std::vector<std::pair<long, std::string>> v;
    for (auto& f : byFunc) {
        v.push_back({ f.second, f.first });
    }
    std::sort(v.rbegin(), v.rend());
    printf("EXTEST samples: %ld\n", g_sampleCount);
    for (size_t i = 0; i < v.size() && i < 45; i++) {
        printf("EXTEST sample %5.1f%% %s\n", 100.0 * v[i].first / std::max(1L, g_sampleCount), v[i].second.c_str());
    }
    fflush(stdout);
}
}
#else
namespace EX
{
void SamplerStart() {}
void SamplerReport() {}
}
#endif
