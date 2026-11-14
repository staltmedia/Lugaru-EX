/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "EX/ExGraphics.hpp"

#include "EX/ExConfig.hpp"
#include "EX/ExProfile.hpp"
#include "Graphic/gamegl.hpp"

#include <cmath>
#include <cstdio>

#ifdef _WIN32
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

namespace EX
{
static float maxAniso = 0;
static int g_refresh = 0;
static int g_extraRows = 0; // borderless: rows past the monitor's bottom edge

int BorderlessExtraRows()
{
    return g_extraRows;
}

void PreWindowGLAttributes(int msaa)
{
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    if (msaa > 1) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa);
    } else {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    }
}

static SDL_Window* tryCreate(int& width, int& height, bool grabInput, SDL_GLContext* outCtx, int msaa)
{
    PreWindowGLAttributes(msaa);

    int display = cfg.monitor;
    if (display < 0 || display >= SDL_GetNumVideoDisplays()) {
        display = 0;
    }
    SDL_Rect bounds;
    SDL_GetDisplayBounds(display, &bounds);
    SDL_DisplayMode desk;
    SDL_GetDesktopDisplayMode(display, &desk);

    if (width <= 0 || height <= 0) {
        width = desk.w;
        height = desk.h;
    }

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (grabInput) {
        flags |= SDL_WINDOW_INPUT_GRABBED;
    }
    // only exclusive fullscreen may minimize when another window is clicked or alt-tabbed to
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, cfg.displayMode == DISPLAY_FULLSCREEN ? "1" : "0");
    g_extraRows = 0;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display), y = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    switch (cfg.displayMode) {
        case DISPLAY_BORDERLESS:
            flags |= SDL_WINDOW_BORDERLESS;
            // borderless windows larger than the monitor make no sense
            if (width > bounds.w) {
                width = bounds.w;
            }
            if (height > bounds.h) {
                height = bounds.h;
            }
            x = bounds.x + (bounds.w - width) / 2;
            y = bounds.y + (bounds.h - height) / 2;
            if (width == bounds.w && height == bounds.h) {
                // An OpenGL window that covers its monitor exactly is taken over by the
                // graphics driver as exclusive fullscreen: clicking another window or
                // alt-tabbing blacked out the screen and minimized it like real fullscreen.
                // One row past the bottom edge keeps it an ordinary (composited) window.
                height += 1;
                g_extraRows = 1;
            }
            break;
        case DISPLAY_FULLSCREEN:
            // entered below, after the display mode (incl. refresh rate) is set
            x = bounds.x;
            y = bounds.y;
            break;
        default:
            flags |= SDL_WINDOW_RESIZABLE; // windowed: drag the edges or maximize
            break;
    }

    SDL_Window* win = SDL_CreateWindow("Lugaru HD (LugaruEX)", x, y, width, height, flags);
    if (!win) {
        fprintf(stderr, "LugaruEX: SDL_CreateWindow(%dx%d msaa=%d) failed: %s\n", width, height, msaa, SDL_GetError());
        return NULL;
    }

    if (cfg.displayMode == DISPLAY_FULLSCREEN) {
        // exact size; the requested refresh rate, else the highest one available
        SDL_DisplayMode best = { 0, 0, 0, 0, 0 };
        for (int i = 0; i < SDL_GetNumDisplayModes(display); i++) {
            SDL_DisplayMode m;
            if (SDL_GetDisplayMode(display, i, &m) != 0 || m.w != width || m.h != height) {
                continue;
            }
            if (cfg.refreshRate > 0 && m.refresh_rate == cfg.refreshRate) {
                best = m;
                break;
            }
            if (m.refresh_rate > best.refresh_rate || (m.refresh_rate == best.refresh_rate && SDL_BITSPERPIXEL(m.format) > SDL_BITSPERPIXEL(best.format))) {
                best = m;
            }
        }
        if (best.w == 0) {
            SDL_DisplayMode want = { 0, width, height, cfg.refreshRate, 0 };
            SDL_GetClosestDisplayMode(display, &want, &best);
        }
        if (best.w) {
            SDL_SetWindowDisplayMode(win, &best);
        }
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN);
        printf("LugaruEX: exclusive fullscreen %dx%d @ %d Hz\n", best.w, best.h, best.refresh_rate);
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        fprintf(stderr, "LugaruEX: SDL_GL_CreateContext(msaa=%d) failed: %s\n", msaa, SDL_GetError());
        SDL_DestroyWindow(win);
        return NULL;
    }
    SDL_GL_MakeCurrent(win, ctx);
    SDL_GL_GetDrawableSize(win, &width, &height);
    *outCtx = ctx;

    SDL_DisplayMode cur;
    if ((cfg.displayMode == DISPLAY_FULLSCREEN ? SDL_GetWindowDisplayMode(win, &cur) : SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(win), &cur)) == 0) {
        g_refresh = cur.refresh_rate;
    }
    printf("LugaruEX: window %dx%d on display %d, %d Hz, simulation %d Hz\n", width, height, SDL_GetWindowDisplayIndex(win), g_refresh, SimRate());
    return win;
}

SDL_Window* CreateGameWindow(int& width, int& height, bool grabInput, SDL_GLContext* outCtx)
{
    // Try the requested MSAA level, then step down, then a safe 640x480 window.
    const int levels[] = { cfg.msaa, 8, 4, 2, 0 };
    for (int lv : levels) {
        if (lv > cfg.msaa) {
            continue;
        }
        int w = width, h = height;
        SDL_Window* win = tryCreate(w, h, grabInput, outCtx, lv);
        if (win) {
            if (lv != cfg.msaa) {
                fprintf(stderr, "LugaruEX: MSAA %dx unavailable, using %dx\n", cfg.msaa, lv);
                cfg.msaa = lv;
            }
            width = w;
            height = h;
            return win;
        }
    }
    cfg.displayMode = DISPLAY_WINDOWED;
    cfg.msaa = 0;
    width = 640;
    height = 480;
    return tryCreate(width, height, grabInput, outCtx, 0);
}

int RefreshRate()
{
    return g_refresh;
}

static int g_simCap = 0; // lowered at run time when this PC can't keep up (0 = none)
void LimitSimRate(int hz)
{
    g_simCap = hz;
}

int SimRate()
{
    if (cfg.simRate > 0) {
        return cfg.simRate;
    }
    // auto: one simulation step per displayed frame on the game's monitor
    int r = g_refresh > 0 ? g_refresh : 120;
    if (r < 60) {
        r = 60;
    }
    if (g_simCap > 0 && r > g_simCap) {
        r = g_simCap;
    }
    if (r > 360) {
        r = 360;
    }
    return r;
}

void InitGLExtras()
{
    // vsync: 1 = always synced (no tearing), 2 = adaptive (tears when below refresh), 0 = off
    if (cfg.vsync == 2) {
        if (SDL_GL_SetSwapInterval(-1) == -1) {
            SDL_GL_SetSwapInterval(1);
        }
    } else {
        SDL_GL_SetSwapInterval(cfg.vsync ? 1 : 0);
    }
    if (cfg.msaa > 1) {
        glEnable(GL_MULTISAMPLE);
    }
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    if (ext && (strstr(ext, "GL_EXT_texture_filter_anisotropic") || strstr(ext, "GL_ARB_texture_filter_anisotropic"))) {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    }
    printf("LugaruEX: GL %s | %s | max AF %.0fx\n", (const char*)glGetString(GL_VERSION),
           (const char*)glGetString(GL_RENDERER), maxAniso);
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
}

void ApplyAnisotropy()
{
    if (cfg.anisotropy > 1 && maxAniso > 1) {
        float a = (float)cfg.anisotropy;
        if (a > maxAniso) {
            a = maxAniso;
        }
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, a);
    }
}

void AfterSwap()
{
    HangBeat(); // (test hook LUGARUEX_TEST_HANG)
    // With vsync, wait for the GPU to finish the frame right after the swap:
    // frame times then follow the display exactly instead of jittering with
    // the driver's queue depth (smoother motion, less input lag).
    // A PC that keeps missing the refresh skips it: there the wait made the CPU and the GPU
    // take turns (each frame's game work, then its drawing, then the next) where they could
    // work side by side - with vsync that could turn 60 fps into 30. PCs that keep up are
    // paced as before. Once switched it stays for the session (switching back and forth
    // would make the pacing itself uneven); it is logged.
    if (cfg.vsync != 1) {
        return;
    }
    static Uint64 last = 0;
    static float missed = 0; // running share of frames that took longer than 1.5 refreshes
    static bool overlap = false;
    const Uint64 now = SDL_GetPerformanceCounter();
    if (last && g_refresh > 0) {
        const double dt = (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        if (dt < 0.25) { // (not a loading hitch)
            missed = missed * 0.99f + (dt > 1.5 / g_refresh ? 0.01f : 0.f);
            if (!overlap && missed > 0.5f) { // (most frames, for over a hundred of them)
                overlap = true;
                printf("LugaruEX: frames keep missing the %d Hz refresh - the CPU and GPU now work side by side\n", g_refresh);
            }
        }
    }
    last = now; // (swap to swap: the whole frame, the wait included)
    if (!overlap) {
        glFinish();
    }
}

#ifdef _WIN32
static void preciseSleepMs(double ms)
{
    // high resolution waitable timer (Windows 10 1803+), falls back to Sleep
    static HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    if (timer) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(ms * 10000.0);
        if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }
    Sleep((DWORD)ms);
}
#endif

void LimitFrameRate()
{
    // -1 = match the display's refresh rate (detected when the window opened)
    const int cap = (cfg.fpsCap < 0) ? g_refresh : cfg.fpsCap;
    if (cap <= 0) {
        return;
    }
    if (cfg.vsync && g_refresh > 0 && cap >= g_refresh) {
        return; // vsync already paces us
    }
    static Uint64 next = 0;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 period = freq / (Uint64)cap;
    Uint64 now = SDL_GetPerformanceCounter();
    if (next == 0 || now > next + period * 4) {
        next = now; // first frame, or we fell far behind (loading): resync
    }
    next += period;
    // sleep most of the wait, spin the last ~0.3 ms for accuracy
    for (;;) {
        now = SDL_GetPerformanceCounter();
        if (now >= next) {
            break;
        }
        const double remainMs = (double)(next - now) * 1000.0 / (double)freq;
        if (remainMs > 0.6) {
#ifdef _WIN32
            preciseSleepMs(remainMs - 0.3);
#else
            SDL_Delay((Uint32)(remainMs - 0.3));
#endif
        }
    }
}

void SetLodBias(float bias)
{
    // The desert's heat haze moves the texture LOD bias to a new random value every frame,
    // switching it per terrain patch, per object and for the sky. Some OpenGL drivers build
    // a new shader for each new value of that fixed-function state, so the desert levels
    // stuttered badly there. Steps of a quarter (a dozen values the driver keeps), and
    // nothing sent when it doesn't change.
    static float current = -1000;
    const float q = floorf(bias * 4 + 0.5f) / 4;
    if (q != current) {
        current = q;
        glTexEnvf(0x8500 /* GL_TEXTURE_FILTER_CONTROL */, 0x8501 /* GL_TEXTURE_LOD_BIAS */, q);
    }
}

float AdjustFov(float fov)
{
    if (fabsf(cfg.fov - 90.f) < 0.01f) {
        return fov;
    }
    // keep the ratio of tan(fov/2) between the requested fov and the 90 deg default
    const double k = tan(cfg.fov * M_PI / 360.0) / tan(90.0 * M_PI / 360.0);
    double t = tan(fov * M_PI / 360.0) * k;
    return (float)(atan(t) * 360.0 / M_PI);
}
}
