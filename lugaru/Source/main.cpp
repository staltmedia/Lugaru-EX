/*
Copyright (C) 2003, 2010 - Wolfire Games
Copyright (C) 2010-2017 - Lugaru contributors (see AUTHORS file)

This file is part of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

Lugaru is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Lugaru.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Game.hpp"

#include "Audio/openal_wrapper.hpp"
#include "Audio/Sounds.hpp"
#include "Graphic/gamegl.hpp"
#include "Platform/Platform.hpp"
#include "User/Settings.hpp"
#include "Menu/Menu.hpp"
#include "Version.hpp"
#include "Level/Dialog.hpp"
#include "EX/ExConfig.hpp"
#include "EX/ExMods.hpp"
#include "EX/ExGraphics.hpp"
#include "EX/ExInterp.hpp"
#include "EX/ExLocal.hpp"
#include "EX/ExNet.hpp"
#include "EX/ExCapture.hpp"
#include "EX/ExLighting.hpp"
#include "EX/ExPad.hpp"
#include "EX/ExProfile.hpp"
#include "Utils/Folders.hpp"
#include "Objects/Object.hpp"
#include "Objects/Person.hpp"

#include <fstream>
#include <iostream>
#include <math.h>
#include <set>
#include <stdio.h>
#include <string.h>
#include <time.h>

using namespace Game;

#ifdef WIN32
#include <shellapi.h>
#include <dbghelp.h>
#include <windows.h>
// LugaruEX: on PCs with two graphics chips (most gaming laptops, some desktops)
// Windows runs an unknown game on the low-power one; these ask the NVIDIA / AMD
// drivers for the fast one.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#else
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#endif

extern int hostile;
extern Terrain terrain;
extern float multiplier;
extern float realmultiplier;
extern int slomo;
extern bool cellophane;
extern float texdetail;

extern bool freeze;
extern bool winfreeze;
extern bool gamestarted;
extern bool stillloading;
extern int mainmenu;

extern float slomospeed;
extern float slomofreq;

extern int difficulty;

extern SDL_Window* sdlwindow;
extern float maxscreenwidth, maxscreenheight;

using namespace std;

set<pair<int, int>> resolutions;

// LugaruEX: raw mouse motion not yet seen by the simulation
static float pendingMouseH = 0;
static float pendingMouseV = 0;
static bool forceSimStep = false; // run at least one sim step this frame
static double exLastRawDt = 0;      // LugaruEX test stats: last measured frame time

// statics/globals (internal only) ------------------------------------------

// Menu defs

int kContextWidth;
int kContextHeight;

//-----------------------------------------------------------------------------------------------------------------------

// OpenGL Drawing

void initGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    swap_gl_buffers();

    // clear all states
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glDisable(GL_LOGIC_OP);
    glDisable(GL_TEXTURE_1D);
    glDisable(GL_TEXTURE_2D);
    glPixelTransferi(GL_MAP_COLOR, GL_FALSE);
    glPixelTransferi(GL_RED_SCALE, 1);
    glPixelTransferi(GL_RED_BIAS, 0);
    glPixelTransferi(GL_GREEN_SCALE, 1);
    glPixelTransferi(GL_GREEN_BIAS, 0);
    glPixelTransferi(GL_BLUE_SCALE, 1);
    glPixelTransferi(GL_BLUE_BIAS, 0);
    glPixelTransferi(GL_ALPHA_SCALE, 1);
    glPixelTransferi(GL_ALPHA_BIAS, 0);

    // set initial rendering states
    glShadeModel(GL_SMOOTH);
    glClearDepth(1.0f);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glCullFace(GL_FRONT);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_DITHER);
    glEnable(GL_COLOR_MATERIAL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glAlphaFunc(GL_GREATER, 0.5f);

    if (CanInitStereo(stereomode)) {
        InitStereo(stereomode);
    } else {
        fprintf(stderr, "Failed to initialize stereo, disabling.\n");
        stereomode = stereoNone;
    }
}

void toggleFullscreen()
{
    fullscreen = !fullscreen;
    Uint32 flags = SDL_GetWindowFlags(sdlwindow);
    if (flags & SDL_WINDOW_FULLSCREEN) {
        flags &= ~SDL_WINDOW_FULLSCREEN;
    } else {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    SDL_SetWindowFullscreen(sdlwindow, flags);
}

SDL_bool sdlEventProc(const SDL_Event& e)
{
    switch (e.type) {
        case SDL_QUIT:
            return SDL_FALSE;

        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_CLOSE) {
                return SDL_FALSE;
            }
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && EX::cfg.displayMode == EX::DISPLAY_WINDOWED) {
                // LugaruEX: the window was resized or maximized: draw at the new size from
                // the next frame on (projection, views and the lighting buffers follow
                // screenwidth/screenheight); the size is kept for next time
                int w = 0, h = 0;
                SDL_GL_GetDrawableSize(sdlwindow, &w, &h);
                if (w >= 64 && h >= 64 && (w != (int)screenwidth || h != (int)screenheight)) {
                    screenwidth = (float)w;
                    screenheight = (float)h;
                    kContextWidth = w;
                    kContextHeight = h;
                    newscreenwidth = w;
                    newscreenheight = h;
                    EX::LocalInvalidateViews();
                    printf("LugaruEX: window resized to %dx%d\n", w, h);
                }
            }
            break;

        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            EX::PadHandleEvent(e); // LugaruEX: plug / unplug controllers any time
            break;

        case SDL_MOUSEMOTION: {
            // LugaruEX: accumulate until the next simulation step consumes it
            static const bool noInput = getenv("LUGARUEX_TEST_NOINPUT") != nullptr; // test hook: ignore the real mouse
            if (!noInput) {
                pendingMouseH += e.motion.xrel;
                pendingMouseV += e.motion.yrel;
            }
        } break;

        case SDL_KEYDOWN:
            if ((e.key.keysym.scancode == SDL_SCANCODE_G) &&
                (e.key.keysym.mod & KMOD_CTRL)) {
                SDL_bool mode = SDL_TRUE;
                if ((SDL_GetWindowFlags(sdlwindow) & SDL_WINDOW_FULLSCREEN) == 0) {
                    mode = (SDL_GetWindowGrab(sdlwindow) ? SDL_FALSE : SDL_TRUE);
                }
                SDL_SetWindowGrab(sdlwindow, mode);
                SDL_SetRelativeMouseMode(mode);
            } else if ((e.key.keysym.scancode == SDL_SCANCODE_RETURN) && (e.key.keysym.mod & KMOD_ALT)) {
                toggleFullscreen();
            } else if ((int)e.key.keysym.scancode == EX::LightingKey() && !e.key.repeat && !console && !mainmenu && !waiting) {
                EX::LightingCycle(); // LugaruEX: next lighting look
            }
            break;
    }
    return SDL_TRUE;
}

// --------------------------------------------------------------------------

static Point gMidPoint;

bool SetUp()
{
    LOGFUNC;

    cellophane = 0;
    texdetail = 4;
    slomospeed = 0.25;
    slomofreq = 8012;

    DefaultSettings();

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO) == -1) {
            fprintf(stderr, "SDL_Init() failed: %s\n", SDL_GetError());
            return false;
        }
    }
    if (!LoadSettings()) {
        fprintf(stderr, "Failed to load config, creating default\n");
        SaveSettings();
    }
    EX::LoadConfig();
    EX::ModsLoad(); // LugaruEX: mods switched on in the launcher
    EX::ApplyKeyBindings(); // LugaruEX: binds from the launcher's Controls tab
    EX::PadInit();
    EX::LocalInit(); // LugaruEX: local co-op players from [Local]
    EX::NetInit();   // LugaruEX: online host / guest from [Multiplayer]
    maxscreenwidth = 16384; // LugaruEX: allow 4K/5K/ultrawide and beyond
    maxscreenheight = 16384;

    if (SDL_GL_LoadLibrary(NULL) == -1) {
        fprintf(stderr, "SDL_GL_LoadLibrary() failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    for (int displayIdx = 0; displayIdx < SDL_GetNumVideoDisplays(); ++displayIdx) {
        for (int i = 0; i < SDL_GetNumDisplayModes(displayIdx); ++i) {
            SDL_DisplayMode mode;
            if (SDL_GetDisplayMode(displayIdx, i, &mode) == -1) {
                continue;
            }
            if ((mode.w < 640) || (mode.h < 480)) {
                continue; // sane lower limit.
            }
            pair<int, int> resolution(mode.w, mode.h);
            resolutions.insert(resolution);
        }
    }

    if (resolutions.empty()) {
        const std::string error = "No suitable video resolutions found.";
        cerr << error << endl;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Lugaru init failed!", error.c_str(), NULL);
        SDL_Quit();
        return false;
    }

    if (commandLineOptions[SHOWRESOLUTIONS]) {
        printf("Available resolutions:\n");
        for (auto resolution = resolutions.begin(); resolution != resolutions.end(); resolution++) {
            printf("  %d x %d\n", (int)resolution->first, (int)resolution->second);
        }
    }

    // LugaruEX: the launcher's LugaruEX.ini decides the display setup;
    // without it we fall back to the game's own config.txt values.
    if (EX::cfg.loaded) {
        kContextWidth = EX::cfg.width;
        kContextHeight = EX::cfg.height;
    } else {
        EX::cfg.width = kContextWidth;
        EX::cfg.height = kContextHeight;
        EX::cfg.displayMode = fullscreen ? EX::DISPLAY_FULLSCREEN : EX::DISPLAY_WINDOWED;
    }
    if (commandLineOptions[FULLSCREEN]) {
        EX::cfg.displayMode = commandLineOptions[FULLSCREEN].last()->type() ? EX::DISPLAY_FULLSCREEN : EX::DISPLAY_WINDOWED;
    }

    SDL_GLContext glctx = NULL;
    sdlwindow = EX::CreateGameWindow(kContextWidth, kContextHeight, !commandLineOptions[NOMOUSEGRAB].last()->type(), &glctx);
    if (!sdlwindow) {
        fprintf(stderr, "SDL_CreateWindow() failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }
    fullscreen = (EX::cfg.displayMode == EX::DISPLAY_FULLSCREEN);
    newscreenwidth = kContextWidth;
    newscreenheight = kContextHeight - EX::BorderlessExtraRows(); // the size chosen, not the extra row

    int dblbuf = 0;
    if ((SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &dblbuf) == -1) || (!dblbuf)) {
        fprintf(stderr, "Failed to get a double-buffered context.\n");
        SDL_Quit();
        return false;
    }

    EX::InitGLExtras();
    EX::LightingInit();

    SDL_ShowCursor(0);
    if (!commandLineOptions[NOMOUSEGRAB].last()->type()) {
        SDL_SetRelativeMouseMode(SDL_TRUE);
    }

    initGL();

    GLint width = kContextWidth;
    GLint height = kContextHeight;
    gMidPoint.h = width / 2;
    gMidPoint.v = height / 2;
    screenwidth = width;
    screenheight = height;

    newdetail = detail;
    newscreenwidth = screenwidth;
    newscreenheight = screenheight - EX::BorderlessExtraRows();

    /* If saved resolution is not in the list, add it to the list (so that itâ€™s selectable in the options) */
    pair<int, int> startresolution(width, height - EX::BorderlessExtraRows());
    if (resolutions.find(startresolution) == resolutions.end()) {
        resolutions.insert(startresolution);
    }

    InitGame();

    return true;
}

static void DoMouse()
{

    // LugaruEX: the original rejected "too fast" motion relative to the frame
    // time; with short fixed steps that dropped fast flicks, so use >= 1/30 s.
    const float window = (realmultiplier > 1.f / 30) ? realmultiplier : 1.f / 30;
    if (mainmenu || ((abs(deltah) < 10 * window * 1000) && (abs(deltav) < 10 * window * 1000))) {
        deltah *= usermousesensitivity;
        deltav *= usermousesensitivity;
        mousecoordh += deltah;
        mousecoordv += deltav;
        if (mousecoordh < 0) {
            mousecoordh = 0;
        } else if (mousecoordh >= kContextWidth) {
            mousecoordh = kContextWidth - 1;
        }
        if (mousecoordv < 0) {
            mousecoordv = 0;
        } else if (mousecoordv >= kContextHeight) {
            mousecoordv = kContextHeight - 1;
        }
    }
}

void DoFrameRate(int update)
{
    static long frames = 0;

    static AbsoluteTime time = { 0, 0 };
    static AbsoluteTime frametime = { 0, 0 };
    AbsoluteTime currTime = UpTime();
    double deltaTime = (float)AbsoluteDeltaToDuration(currTime, frametime);

    if (0 > deltaTime) { // if negative microseconds
        deltaTime /= -1000000.0;
    } else { // else milliseconds
        deltaTime /= 1000.0;
    }

    multiplier = deltaTime;
    if (multiplier < .001) {
        multiplier = .001;
    }
    if (multiplier > 10) {
        multiplier = 10;
    }
    if (update) {
        frametime = currTime; // reset for next time interval
    }

    deltaTime = (float)AbsoluteDeltaToDuration(currTime, time);

    if (0 > deltaTime) { // if negative microseconds
        deltaTime /= -1000000.0;
    } else { // else milliseconds
        deltaTime /= 1000.0;
    }
    frames++;
    if (0.001 <= deltaTime) { // has update interval passed
        if (update) {
            time = currTime; // reset for next time interval
            frames = 0;
        }
    }
}

// Scale a real-time delta the same way the original game did per frame.
static float GameTimeScale()
{
    float m = gamespeed;
    if (difficulty == 1) {
        m *= .9;
    }
    if (difficulty == 0) {
        m *= .8;
    }
    if (loading == 4) {
        m *= .00001;
    }
    if (slomo && (!mainmenu || EX::NetIsHost() || EX::NetIsGuest())) {
        m *= slomospeed;
    }
    return m;
}

// One simulation update of `dt` real seconds (the body of the original
// per-frame update: TickOnce, N physics substeps, TickOnceAfter).
static void SimStep(float dt)
{
    static const float sps = 200;

    multiplier = dt;
    if (multiplier > .6) {
        multiplier = .6;
    }
    int count = multiplier * sps;
    if (count < 2) {
        count = 2;
    }

    realmultiplier = multiplier;
    multiplier *= GameTimeScale();
    float oldmult = multiplier;
    multiplier /= (float)count;

    deltah = pendingMouseH;
    deltav = pendingMouseV;
    pendingMouseH = 0;
    pendingMouseV = 0;

    DoMouse();

    EX::NetFxShare fxShare; // LugaruEX: effects the simulation makes go to online guests

    TickOnce();
    EX::LocalSimLook(); // players 2-4: right stick look
    EX::NetGuestStep(); // online guest: everyone where the host says

    for (int i = 0; i < count; i++) {
        Tick();
    }
    multiplier = oldmult;

    TickOnceAfter();

    // Weapon physics used to run once per rendered frame (inside DrawGLScene),
    // so its behaviour depended on the frame rate. It now runs per sim step.
    if (freeze || winfreeze || (mainmenu && gameon && !EX::NetIsHost()) || (!gameon && gamestarted)) {
        multiplier = 0;
    }
    if (!EX::NetIsGuest()) { // an online guest's weapons are placed by the host
        weapons.DoStuff();
    }
    multiplier = oldmult;
}

void DoUpdate()
{
    // LugaruEX fixed timestep: the simulation always advances in steps of
    // 1/SimRate seconds, independent of the rendering frame rate. Rendering
    // blends between the last two simulation states.
    static Uint64 lastCounter = 0;
    static double accumulator = 0;
    const Uint64 now = SDL_GetPerformanceCounter();
    double frameDt = lastCounter ? (double)(now - lastCounter) / (double)SDL_GetPerformanceFrequency() : 1.0 / 60.0;
    exLastRawDt = frameDt;
    lastCounter = now;
    if (frameDt > 0.25) {
        frameDt = 0.25; // after loading / alt-tab: don't fast-forward
        EX::InterpInvalidate();
    }
    if (frameDt < 1e-5) {
        frameDt = 1e-5;
    }
    {
        // test hook LUGARUEX_TEST_SLOW=ms: pretend to be a slow PC (sleep every frame)
        static const int slowMs = getenv("LUGARUEX_TEST_SLOW") ? atoi(getenv("LUGARUEX_TEST_SLOW")) : 0;
        if (slowMs > 0) {
            SDL_Delay(slowMs);
        }
    }
    // With vsync the real frame interval is exactly a multiple of the refresh
    // period; the measured value just jitters around it. Snapping removes
    // that jitter so every frame advances the simulation by the same amount.
    if (EX::cfg.vsync && EX::RefreshRate() > 0) {
        const double period = 1.0 / EX::RefreshRate();
        for (int k = 1; k <= 4; k++) {
            if (fabs(frameDt - k * period) < 0.1 * period) {
                frameDt = k * period;
                break;
            }
        }
    }
    fps = 1 / frameDt;

    EX::ProfFrameStart(); // (LUGARUEX_PROFILE: frame-time breakdown in the log)
    EX::PadFrame(frameDt, pendingMouseH, pendingMouseV); // controller: look, menu navigation
    EX::LocalFrame(frameDt);                             // local co-op: drop-in, players 2-4 look
    EX::NetFrame(frameDt);                               // online: packets in, inputs out
    EX_PROF("input+net");
    const double step = 1.0 / EX::SimRate();
    accumulator += frameDt;
    if (forceSimStep && accumulator < step) {
        accumulator = step;
    }
    forceSimStep = false;
    int steps = 0;
    const int maxSteps = 10;
    // test hook LUGARUEX_TEST_FREEZE=n: the world stops after n simulation steps in a level
    // (no simulation, no draw-time effects): every run then draws the very same frame,
    // for comparing pictures between builds
    static const long freezeAt = getenv("LUGARUEX_TEST_FREEZE") ? atol(getenv("LUGARUEX_TEST_FREEZE")) : -1;
    static long levelSteps = 0;
    const bool frozen = freezeAt >= 0 && Game::gameon && !mainmenu && levelSteps >= freezeAt;
    if (frozen) {
        accumulator = 0;
    }
    while (accumulator >= step && steps < maxSteps) {
        if (Game::gameon && !mainmenu) {
            levelSteps++;
        }
        const int levelBefore = loading;
        SimStep((float)step);
        accumulator -= step;
        steps++;
        if (loading != levelBefore) {
            EX::InterpInvalidate();
            EX::LocalInvalidateViews();
        }
        EX::InterpCapture();
        EX::LocalCaptureViews();
        EX::NetAfterSimStep(step); // online host: snapshots out
    }
    if (steps == maxSteps) {
        accumulator = 0; // can't keep up (very slow machine): slow down instead of spiralling
    }
    EX_PROF("sim");
    // A PC that can't show half as many frames as it simulates spends most of each
    // frame catching up (several steps per frame), which makes the frames slower
    // still: halve the simulation rate (not below 60 Hz) until it keeps up.
    {
        static double perfT = 0;
        static long perfFrames = 0;
        if (frameDt > 0.25 || loading || mainmenu || !Game::gameon) {
            perfT = 0; // loading hitches and menus don't count
            perfFrames = 0;
        } else {
            perfT += frameDt;
            perfFrames++;
            if (perfT >= 3.0) {
                const double f = perfFrames / perfT;
                if (f < 50) { // a slow PC: its log shows what the time goes on (nothing on screen)
                    char why[64];
                    snprintf(why, sizeof(why), "only %.0f fps", f);
                    EX::ProfAuto(why);
                }
                if (EX::cfg.simRate <= 0 && EX::SimRate() > 60 && f < EX::SimRate() * 0.5) {
                    const int nr = std::max(60, EX::SimRate() / 2);
                    printf("LugaruEX: only %.0f fps here - simulation lowered from %d to %d Hz\n", f, EX::SimRate(), nr);
                    EX::LimitSimRate(nr);
                    EX::InterpInvalidate();
                }
                perfT = 0;
                perfFrames = 0;
            }
        }
    }
    float alpha = EX::cfg.interpolation ? (float)(accumulator / step) : 1.f;

    // Draw-time effects (particles, flashes...) advance by real frame time,
    // exactly like the original one-update-per-frame loop did.
    realmultiplier = frozen ? 0.f : (float)frameDt;
    multiplier = frozen ? 0.f : (float)frameDt * GameTimeScale();
    if (frozen) {
        alpha = 1; // the last step as it is
        extern float windvar, smoketex;
        windvar = smoketex = 0; // (they also ran in the menu before the level, for however long that took)
    }

    float pendYaw = 0, pendPitch = 0;
    if (!mainmenu && (Dialog::directing || !Dialog::inDialog())) {
        pendYaw = pendingMouseH * usermousesensitivity * .7f;
        pendPitch = (invertmouse ? -1.f : 1.f) * pendingMouseV * usermousesensitivity * .7f;
    }
    if (EX::cfg.interpolation) {
        EX::InterpApply(alpha, pendYaw, pendPitch);
    }
    /* - Debug code to test how many channels were active on average per frame
        static long frames = 0;

        static AbsoluteTime start = {0,0};
        AbsoluteTime currTime = UpTime ();
        static int num_channels = 0;

        num_channels += OPENAL_GetChannelsPlaying();
        double deltaTime = (float) AbsoluteDeltaToDuration (currTime, start);

        if (0 > deltaTime)  // if negative microseconds
            deltaTime /= -1000000.0;
        else                // else milliseconds
            deltaTime /= 1000.0;

        ++frames;

        if (deltaTime >= 1)
        {
            start = currTime;
            float avg_channels = (float)num_channels / (float)frames;

            ofstream opstream("log.txt",ios::app);
            opstream << "Average frame count: ";
            opstream << frames;
            opstream << " frames - ";
            opstream << avg_channels;
            opstream << " per frame.\n";
            opstream.close();

            frames = 0;
            num_channels = 0;
        }
    */
    EX_PROF("interp");
    if (stereomode == stereoNone) {
        EX::DrawViews(alpha, [] { DrawGLScene(stereoCenter); }); // LugaruEX: one view per local player
    } else {
        DrawGLScene(stereoLeft);
        DrawGLScene(stereoRight);
    }
    EX::ProfFrameEnd();
    EX::InterpRestore();
    EX::LimitFrameRate();
}

// --------------------------------------------------------------------------

void CleanUp(void)
{
    EX::CaptureShutdown(); // LugaruEX: finish a recording so its file is complete
    EX::NetShutdown();     // LugaruEX: tell the other side we left
    LOGFUNC;

    delete[] commandLineOptionsBuffer;

    SDL_Quit();
}

// --------------------------------------------------------------------------

static bool IsFocused()
{
    return ((SDL_GetWindowFlags(sdlwindow) & SDL_WINDOW_INPUT_FOCUS) != 0);
}

// LugaruEX test hooks: is the environment variable set (and its value, if wanted)
static bool exEnv(const char* name, char* value = nullptr, size_t size = 0)
{
    const char* v = getenv(name);
    if (!v) {
        return false;
    }
    if (value && size) {
        snprintf(value, size, "%s", v);
    }
    return true;
}


const option::Descriptor usage[] =
    {
      { UNKNOWN, 0, "", "", option::Arg::None, "USAGE: lugaru [options]\n\n"
                                               "Options:" },
      { VERSION, 0, "v", "version", option::Arg::None, " -v, --version     Print version and exit." },
      { HELP, 0, "h", "help", option::Arg::None, " -h, --help        Print usage and exit." },
      { FULLSCREEN, 1, "f", "fullscreen", option::Arg::None, " -f, --fullscreen  Start the game in fullscreen mode." },
      { FULLSCREEN, 0, "w", "windowed", option::Arg::None, " -w, --windowed    Start the game in windowed mode (default)." },
      { NOMOUSEGRAB, 1, "", "nomousegrab", option::Arg::None, " --nomousegrab     Disable mousegrab." },
      { NOMOUSEGRAB, 0, "", "mousegrab", option::Arg::None, " --mousegrab       Enable mousegrab (default)." },
      { SOUND, 1, "", "nosound", option::Arg::None, " --nosound         Disable sound." },
      { OPENALINFO, 0, "", "openal-info", option::Arg::None, " --openal-info     Print info about OpenAL at launch." },
      { SHOWRESOLUTIONS, 0, "", "showresolutions", option::Arg::None, " --showresolutions List the resolutions found by SDL at launch." },
      { DEVTOOLS, 0, "d", "devtools", option::Arg::None, " -d, --devtools    Enable dev tools: console, level editor and debug info." },
      { CMD, 0, "c", "command", option::Arg::Optional, " -c, --command    Run this command at game start. May be used to load a map." },
      { EXLEVEL, 0, "", "exlevel", option::Arg::Optional, " --exlevel=MAP     LugaruEX: load MAP directly (creates a profile if needed)." },
      { EXQUIT, 0, "", "exquit", option::Arg::Optional, " --exquit=SEC      LugaruEX: quit after SEC seconds (testing)." },
      { 0, 0, 0, 0, 0, 0 }
    };

option::Option commandLineOptions[commandLineOptionsNumber];
option::Option* commandLineOptionsBuffer;

#ifdef _WIN32
// LugaruEX: on a crash, log where it happened and write a minidump next to the log.
static LONG WINAPI exCrashHandler(EXCEPTION_POINTERS* ep)
{
    HMODULE self = GetModuleHandleA(NULL);
    const uintptr_t addr = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    fprintf(stderr, "\nLugaruEX CRASH: exception 0x%08lX at LugaruEX.exe+0x%llX\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode, (unsigned long long)(addr - (uintptr_t)self));
    fflush(stderr);
    HMODULE dbg = LoadLibraryA("dbghelp.dll");
    if (dbg) {
        typedef BOOL(WINAPI * MiniDumpFn)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
        MiniDumpFn fn = (MiniDumpFn)GetProcAddress(dbg, "MiniDumpWriteDump");
        const std::string path = Folders::getUserDataPath() + "crash.dmp";
        HANDLE f = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fn && f != INVALID_HANDLE_VALUE) {
            // must use the real (4-byte packed) dbghelp struct, or the call fails
            MINIDUMP_EXCEPTION_INFORMATION info;
            info.ThreadId = GetCurrentThreadId();
            info.ExceptionPointers = ep;
            info.ClientPointers = FALSE;
            fn(GetCurrentProcess(), GetCurrentProcessId(), f, MiniDumpWithIndirectlyReferencedMemory, &info, NULL, NULL);
            fprintf(stderr, "LugaruEX: crash dump written to %s\n", path.c_str());
        }
        if (f != INVALID_HANDLE_VALUE) {
            CloseHandle(f);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// LugaruEX: a C++ error (e.g. "invalid vector subscript") is caught at the very end, far from
// where it happened - remember the call stack of each throw so the report can say where.
static void* g_throwStack[32];
static int g_throwFrames = 0;
static LONG WINAPI exThrowRecorder(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == 0xE06D7363) { // MSVC C++ exception
        g_throwFrames = CaptureStackBackTrace(1, 32, g_throwStack, NULL); // (not this function)
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static std::string exThrowSite(bool all)
{
    std::string s;
    int shown = 0;
    for (int i = 0; i < g_throwFrames && shown < (all ? 32 : 8); i++) {
        const std::string a = EX::CodeAddress(g_throwStack[i]);
        if (!all && a.compare(0, 12, "LugaruEX.exe") != 0) {
            continue; // the message box shows the game's own frames
        }
        s += (shown ? "\n  " : "  ") + a;
        shown++;
    }
    return s;
}
#else
// LugaruEX: on a crash, log the signal and the call stack as module+offset
// (symbolize with: addr2line -fCe lugaruex.debug 0xOFFSET)
static void exSignalHandler(int sig)
{
    signal(sig, SIG_DFL);
    void* st[48];
    const int n = backtrace(st, 48);
    fprintf(stderr, "\nLugaruEX CRASH: signal %d (%s)\n", sig, strsignal(sig));
    for (int i = 1; i < n; i++) {
        fprintf(stderr, "  %s\n", EX::CodeAddress(st[i]).c_str());
    }
    fflush(stderr);
    raise(sig);
}
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    // LugaruEX: render at the monitor's real resolution (not DPI-virtualized,
    // which made Windows upscale a smaller image on scaled displays).
    {
        typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
        SetCtxFn setCtx = (SetCtxFn)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
        if (!setCtx || !setCtx((HANDLE)-4 /*PER_MONITOR_AWARE_V2*/)) {
            SetProcessDPIAware();
        }
        SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
    }
    SetUnhandledExceptionFilter(exCrashHandler);
    AddVectoredExceptionHandler(0, exThrowRecorder);
    // LugaruEX: GUI exe without a console. Unless the output is already
    // redirected, write it to %APPDATA%\LugaruEX\LugaruEX.log (the previous
    // run is kept as LugaruEX.prev.log).
    {
        HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
        // always log to the file (Steam passes its own handles, which swallowed
        // the output); set LUGARUEX_STDOUT=1 to keep stdout/stderr instead
        (void)h;
        if (GetEnvironmentVariableA("LUGARUEX_STDOUT", NULL, 0) == 0) {
            const std::string logPath = Folders::getUserDataPath() + "LugaruEX.log";
            const std::string prevPath = Folders::getUserDataPath() + "LugaruEX.prev.log";
            MoveFileExA(logPath.c_str(), prevPath.c_str(), MOVEFILE_REPLACE_EXISTING);
            freopen(logPath.c_str(), "w", stdout);
            freopen(logPath.c_str(), "a", stderr);
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
        }
        printf("LugaruEX %s\n", EX_VERSION_STRING);
    }
#else
    for (int s : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT }) {
        signal(s, exSignalHandler);
    }
    // LugaruEX: log to ~/.local/share/LugaruEX/LugaruEX.log (the previous run is kept as
    // LugaruEX.prev.log); set LUGARUEX_STDOUT=1 to keep stdout/stderr instead
    if (!getenv("LUGARUEX_STDOUT")) {
        const std::string logPath = Folders::getUserDataPath() + "LugaruEX.log";
        const std::string prevPath = Folders::getUserDataPath() + "LugaruEX.prev.log";
        rename(logPath.c_str(), prevPath.c_str());
        if (freopen(logPath.c_str(), "w", stdout) && freopen(logPath.c_str(), "a", stderr)) {
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
        }
    }
    printf("LugaruEX %s\n", EX_VERSION_STRING);
#endif
    argc -= (argc > 0);
    argv += (argc > 0); // skip program name argv[0] if present
    option::Stats stats(true, usage, argc, argv);
    if (commandLineOptionsNumber != stats.options_max) {
        std::cerr << "Found incorrect command line option number" << std::endl;
        return 1;
    }
    commandLineOptionsBuffer = new option::Option[stats.buffer_max];
    option::Parser parse(true, usage, argc, argv, commandLineOptions, commandLineOptionsBuffer);

    if (parse.error()) {
        delete[] commandLineOptionsBuffer;
        return 1;
    }

    // Always start by printing the version and info to the stdout
    std::cout << "--------------------------------------------------------------------------\n"
              << "Lugaru HD: The Rabbit's Foot, by Wolfire Games and the OSS Lugaru project.\n\n"
              << "Licensed under the GPL 2.0+ and CC-BY-SA 3.0 and 4.0 licenses.\n"
              << "More information, updates and bug reports at http://osslugaru.gitlab.io\n"
              << std::endl;

    std::cout << "Version " + VERSION_STRING + " -- " + VERSION_BUILD_TYPE + " build\n"
              << "--------------------------------------------------------------------------\n"
              << std::endl;

    if (commandLineOptions[VERSION]) {
        // That was enough, quit.
        delete[] commandLineOptionsBuffer;
        return 0;
    }

    if (commandLineOptions[HELP]) {
        option::printUsage(std::cout, usage);
        delete[] commandLineOptionsBuffer;
        return 0;
    }

    if (option::Option* opt = commandLineOptions[UNKNOWN]) {
        std::cerr << "Unknown option: " << opt->name << "\n";
        option::printUsage(std::cerr, usage);
        delete[] commandLineOptionsBuffer;
        return 1;
    }

    EX::ChdirToExe(); // LugaruEX: work from the game folder (the one with Data/)

    LOGFUNC;

#ifdef NDEBUG
    try {
#endif
        {
            newGame();

            if (!SetUp()) {
                delete[] commandLineOptionsBuffer;
                return 42;
            }

            // LugaruEX: dev tools (console on ~, Alt+key debug keys, Shift+K skip, map editor) are always on
            {
                devtools = true;
            }

            bool gameDone = false;
            bool gameFocused = true;

            srand(time(nullptr));

            if (commandLineOptions[CMD].count() > 0) {
                devtools = true;
                Menu::startChallengeLevel(1);
                for (option::Option* opt = commandLineOptions[CMD]; opt; opt = opt->next()) {
                    if (opt->arg && (strlen(opt->arg) > 0)) {
                        cmd_dispatch(opt->arg);
                    }
                }
            }

            // LugaruEX test hooks
            const bool exTest = commandLineOptions[EXQUIT].count() > 0;
            Uint32 exQuitAt = 0;
            if (exTest && commandLineOptions[EXQUIT].last()->arg) {
                exQuitAt = SDL_GetTicks() + (Uint32)(atof(commandLineOptions[EXQUIT].last()->arg) * 1000);
            }
            if (commandLineOptions[EXLEVEL].count() > 0 && commandLineOptions[EXLEVEL].last()->arg) {
                const char* lvl = commandLineOptions[EXLEVEL].last()->arg;
                bool ok = true;
                if (strncmp(lvl, "campaign:", 9) == 0) {
                    if (!Account::hasActive()) {
                        if (Account::getNbAccounts() > 0) {
                            Account::setActive(0);
                        } else {
                            Account::add("");
                        }
                    }
                    if (const char* c = getenv("LUGARUEX_TEST_CAMPAIGN")) { // test hook: which campaign (e.g. one from a mod)
                        Account::active().setCurrentCampaign(c);
                    }
                    Menu::startCampaignLevel(atoi(lvl + 9), false);
                } else if (strncmp(lvl, "challenge:", 10) == 0) {
                    if (!Account::hasActive()) {
                        if (Account::getNbAccounts() > 0) {
                            Account::setActive(0);
                        } else {
                            Account::add("");
                        }
                    }
                    Menu::startChallengeLevel(atoi(lvl + 10)); // exactly what the Challenge menu does
                } else {
                    ok = Menu::startLevelByName(lvl, strcmp(lvl, "tutorial") == 0);
                }
                if (exEnv("LUGARUEX_TEST_FIRE") && !Person::players.empty()) {
                    Person::players[0]->CatchFire(); // test hook: check fire lighting
                    // and set the bush closest to player 1 alight (guests must see it burn)
                    int best = -1;
                    float bestD = 1e30f;
                    for (size_t i = 0; i < Object::objects.size(); i++) {
                        if (Object::objects[i]->type == bushtype) {
                            const float d = distsq(&Object::objects[i]->position, &Person::players[0]->coords);
                            if (d < bestD) {
                                bestD = d;
                                best = (int)i;
                            }
                        }
                    }
                    if (best >= 0) {
                        Object::objects[best]->onfire = 1;
                        printf("EXTEST bush %d set on fire, %.1f from player 1\n", best, sqrtf(bestD));
                    }
                }
                if (exEnv("LUGARUEX_TEST_SLOMO")) {
                    extern int slomo;
                    extern float slomodelay;
                    slomo = 1; // test hook: hold slow motion for the whole run
                    slomodelay = 1000;
                }
                if (exEnv("LUGARUEX_TEST_CYCLE")) {
                    EX::LightingCycle(); // test hook: same as pressing R
                }
                printf("EXTEST level=%s load=%s players=%d at %.2fs\n", lvl, ok ? "ok" : "FAILED", (int)Person::players.size(), SDL_GetTicks() / 1000.f);
                if (exQuitAt) {
                    exQuitAt = SDL_GetTicks() + (exQuitAt - SDL_GetTicks()); // count from after loading
                }
            }
            long exFrames = 0;
            double exWorstMs = 0;
            long exMissed = 0, exCounted = 0;
            const Uint32 exStart = SDL_GetTicks();

            while (!gameDone && !tryquit) {
                if (exTest) {
                    exFrames++;
                    if (SDL_GetTicks() - exStart > 1000) { // skip the first second (warm-up)
                        static double worst = 0;
                        static long missed = 0, counted = 0;
                        counted++;
                        if (exLastRawDt > worst) {
                            worst = exLastRawDt;
                        }
                        if (EX::RefreshRate() > 0 && exLastRawDt > 1.5 / EX::RefreshRate()) {
                            missed++;
                        }
                        exWorstMs = worst * 1000;
                        exMissed = missed;
                        exCounted = counted;
                    }
                    // test hook: knock player 1 out after 3 s (co-op: the level must go on)
                    static bool exKilled = false;
                    if (!exKilled && SDL_GetTicks() - exStart > 3000 && exEnv("LUGARUEX_TEST_KILLP1") &&
                        !Person::players.empty() && !Game::loading) {
                        exKilled = true;
                        Person::players[0]->DoDamage(1000);
                        printf("EXTEST killed p1 at %.1fs\n", (SDL_GetTicks() - exStart) / 1000.f);
                    }
                    // test hook (host) LUGARUEX_TEST_KILLEACH=p1|guest: knock that player out 3 s into
                    // every level (spectating after level changes)
                    {
                        static float exLastLt = 0;
                        static bool exKillDone = false;
                        char who[16] = {};
                        if (exEnv("LUGARUEX_TEST_KILLEACH", who, sizeof(who)) && !Person::players.empty() && !EX::NetIsGuest()) {
                            if (Game::leveltime < exLastLt) {
                                exKillDone = false; // a new level (or restart)
                            }
                            exLastLt = Game::leveltime;
                            if (!exKillDone && Game::leveltime > 3 && !Game::loading && Game::gameon) {
                                exKillDone = true;
                                for (size_t i = 0; i < Person::players.size(); i++) {
                                    const bool pick = strcmp(who, "guest") == 0 ? EX::RemotePerson((int)i) : i == 0;
                                    if (pick) {
                                        Person::players[i]->DoDamage(1000);
                                        printf("EXTEST killed person %d at level time %.1f\n", (int)i, Game::leveltime);
                                    }
                                }
                            }
                        }
                    }
                    // test hook (host): at 9 s put the online guest 5 units in front of the calm enemy
                    // farthest from player 1, as if its last check from this PC's camera found it
                    // hidden (=stale: the old behaviour); at 13 s report whether it noticed the guest
                    static int exSneakEnemy = -2;
                    static char exSneakMode[16] = {};
                    if (exSneakEnemy == -2 && SDL_GetTicks() - exStart > 9000 && exEnv("LUGARUEX_TEST_SNEAK", exSneakMode, sizeof(exSneakMode)) && !Game::loading) {
                        exSneakEnemy = -1;
                        int guest = -1;
                        for (size_t i = 0; i < Person::players.size(); i++) {
                            if (EX::RemotePerson((int)i)) {
                                guest = (int)i;
                            }
                        }
                        float bestD = 0;
                        XYZ bestSpot;
                        for (size_t i = 1; i < Person::players.size() && guest >= 0 && exSneakEnemy < 0; i++) {
                            Person& e = *Person::players[i];
                            const float d = distsq(&e.coords, &Person::players[0]->coords);
                            if (e.isPlayerControlled() || e.dead || e.aitype != passivetype || d < 900) {
                                continue;
                            }
                            XYZ ahead = e.facing; // where it looks
                            ahead.y = 0;
                            Normalise(&ahead);
                            // a spot in front of it with nothing in between (not behind a wall)
                            XYZ eyeE = DoRotation(e.jointPos(head), 0, e.yaw, 0) * e.scale + e.coords;
                            for (int ang = -60; ang <= 60; ang += 15) {
                                XYZ spot = e.coords + DoRotation(ahead, 0, (float)ang, 0) * 5;
                                spot.y = e.coords.y; // (it may stand on a roof)
                                XYZ eyeG = spot;
                                eyeG.y += 1.5f;
                                if (Object::checkcollide(eyeE, eyeG) == -1) {
                                    exSneakEnemy = (int)i;
                                    bestD = d;
                                    bestSpot = spot;
                                    break;
                                }
                            }
                        }
                        if (exSneakEnemy >= 0) {
                            Person& e = *Person::players[exSneakEnemy];
                            Person& g = *Person::players[guest];
                            g.coords = bestSpot;
                            g.velocity = 0;
                            if (strcmp(exSneakMode, "stale") == 0) {
                                e.occluded = 30;
                            }
                            printf("EXTEST sneak: guest %d put in front of enemy %d (%.0f from player 1)\n", guest, exSneakEnemy, sqrtf(bestD));
                        } else {
                            printf("EXTEST sneak: no guest or no calm enemy far from player 1\n");
                        }
                    }
                    if (exSneakEnemy >= 0 && strcmp(exSneakMode, "stale") == 0) {
                        Person::players[exSneakEnemy]->occluded = 30; // as if this PC's camera could not see it
                    }
                    if (exSneakEnemy >= 0 && SDL_GetTicks() - exStart > 13000) {
                        Person& e = *Person::players[exSneakEnemy];
                        printf("EXTEST sneak: enemy %d aitype=%d (%s) occluded=%d howactive=%d target=%d remote=%d farOccluded=%d for %d\n", exSneakEnemy, (int)e.aitype,
                               e.aitype == passivetype ? "did not notice" : "noticed", e.occluded, (int)e.howactive, e.exTarget,
                               (int)EX::RemotePerson(e.exTarget), e.exFarOccluded, e.exFarFor);
                        exSneakEnemy = -3;
                    }
                    // test hook: a dead player 1 switches to watching the next player at 5 s
                    static bool exSpec = false;
                    if (exKilled && !exSpec && SDL_GetTicks() - exStart > 5000 && exEnv("LUGARUEX_TEST_SPECTATE")) {
                        exSpec = true;
                        EX::SpectateNext(0);
                    }
                    // LUGARUEX_TEST_SPECTATE=back: keep pressing every 1.5 s, round to your own body
                    static Uint32 exSpecNext = 0;
                    char exSpecMode[16] = {};
                    if (exSpec && exEnv("LUGARUEX_TEST_SPECTATE", exSpecMode, sizeof(exSpecMode)) &&
                        strcmp(exSpecMode, "back") == 0 && SDL_GetTicks() - exStart > 6500 && SDL_GetTicks() > exSpecNext) {
                        exSpecNext = SDL_GetTicks() + 1500;
                        if (EX::ViewSlot() == 0) {
                            EX::SpectateNext(0);
                        }
                    }
                    {
                        // test hook LUGARUEX_TEST_MAPMENU=seconds: go to the campaign map and stay there
                        static const float mapMenuAt = getenv("LUGARUEX_TEST_MAPMENU") ? (float)atof(getenv("LUGARUEX_TEST_MAPMENU")) : 0.f;
                        static bool mapMenuDone = false;
                        if (mapMenuAt > 0 && !mapMenuDone && (SDL_GetTicks() - exStart) / 1000.f > mapMenuAt) {
                            mapMenuDone = true;
                            mainmenu = 5;
                            printf("EXTEST campaign map (map progress %d)\n", EX::CampaignMapProgress(Account::active().getCurrentCampaign()));
                        }
                        // test hook LUGARUEX_TEST_MENUNEXT=seconds: go back to the main menu, then
                        // pick the next challenge there, like a player does after winning
                        static const float menuNextAt = getenv("LUGARUEX_TEST_MENUNEXT") ? (float)atof(getenv("LUGARUEX_TEST_MENUNEXT")) : 0.f;
                        static int menuNextStep = 0;
                        static Uint32 menuNextT = 0;
                        if (menuNextAt > 0 && menuNextStep == 0 && (SDL_GetTicks() - exStart) / 1000.f > menuNextAt) {
                            menuNextStep = 1;
                            menuNextT = SDL_GetTicks();
                            mainmenu = 5; // the challenge list
                            Game::gameon = 0; // the level is over (guests go back to waiting)
                            printf("EXTEST back to the menu\n");
                        } else if (menuNextStep == 1 && SDL_GetTicks() - menuNextT > 2000) {
                            menuNextStep = 2;
                            printf("EXTEST picking challenge %d from the menu\n", targetlevel + 2);
                            Menu::startChallengeLevel(targetlevel + 1);
                        }
                    }
                    static Uint32 exLastLoadLog = 0;
                    if (Game::loading && SDL_GetTicks() - exLastLoadLog > 1000) {
                        exLastLoadLog = SDL_GetTicks();
                        printf("EXTEST level reloading at %.1fs\n", (SDL_GetTicks() - exStart) / 1000.f);
                    }
                    if (exQuitAt && SDL_GetTicks() >= exQuitAt) {
                        const float secs = (SDL_GetTicks() - exStart) / 1000.f;
                        printf("EXTEST done frames=%ld avgfps=%.1f players=%d", exFrames, exFrames / secs, (int)Person::players.size());
                        printf(" worstframe=%.2fms missedrefresh=%ld/%ld", exWorstMs, exMissed, exCounted);
                        if (!Person::players.empty()) {
                            const XYZ& c = Person::players[0]->coords;
                            printf(" p0=(%.2f,%.2f,%.2f) p0dead=%d", c.x, c.y, c.z, (int)Person::players[0]->dead);
                            // local co-op players: damage taken, and how many enemies are after each
                            for (size_t i = 0; i < Person::players.size(); i++) {
                                Person& h = *Person::players[i];
                                if (!h.isPlayerControlled()) {
                                    continue;
                                }
                                int chasers = 0;
                                for (auto& e : Person::players) {
                                    if (!e->isPlayerControlled() && !e->dead && e->exTarget == (int)i) {
                                        chasers++;
                                    }
                                }
                                printf(" human%d[dmg=%.0f dead=%d targetedBy=%d at %.1f,%.1f,%.1f anim=%d head=%.0f/%.0f]", (int)i, h.damage + h.permanentdamage, (int)h.dead, chasers, h.coords.x, h.coords.y, h.coords.z, h.animTarget, h.headyaw, h.headpitch);
                            }
                        }
                        {
                            // music state: which track is selected, how loud each is, and whether it loaded
                            extern float musicvolume[4];
                            printf(" music=%s vol[level %.0f fight %.0f menu %.0f] loaded[level %d fight %d menu %d alarm %d]",
                                   Game::musictype == stream_fighttheme ? "fight" : Game::musictype == stream_menutheme ? "menu" : "level",
                                   musicvolume[0], musicvolume[1], musicvolume[2], samp[stream_grasstheme] != nullptr,
                                   samp[stream_fighttheme] != nullptr, samp[stream_menutheme] != nullptr, samp[alarmsound] != nullptr);
                        }
                        {
                            // fire: burning objects (campfires always), characters on fire, and AI awareness
                            int objFire = 0, campfires = 0, onFire = 0, alerted = 0;
                            for (auto& o : Object::objects) {
                                objFire += o->onfire && o->type != firetype;
                                campfires += o->type == firetype;
                            }
                            for (auto& p : Person::players) {
                                onFire += p->onfire;
                                alerted += !p->isPlayerControlled() && p->aitype != passivetype;
                            }
                            printf(" fire[objects %d campfires %d people %d] alertedAI=%d", objFire, campfires, onFire, alerted);
                            extern int g_exWeaponShines, g_exWeaponDrips;
                            printf(" weaponfx[glints %d drips %d]", g_exWeaponShines, g_exWeaponDrips);
                        }
                        printf("\n");
                        fflush(stdout);
                        glReadBuffer(GL_FRONT); // last presented frame
                        save_screenshot("extest.png");
                        glReadBuffer(GL_BACK);
                        break;
                    }
                }
                // LugaruEX: online the game keeps running in the background (alt-tab must not
                // freeze the host's world or stop a guest's input); single player still waits
                if (IsFocused() || exTest || EX::NetIsHost() || EX::NetIsGuest()) {
                    gameFocused = true;

                    // check windows messages

                    SDL_Event e;
                    if (!waiting) {
                        // message pump
                        while (SDL_PollEvent(&e)) {
                            if (!sdlEventProc(e)) {
                                gameDone = true;
                                break;
                            }
                        }
                    }

                    // game
                    DoUpdate();
                } else {
                    if (gameFocused) {
                        // allow game chance to pause
                        gameFocused = false;
                        forceSimStep = true;
                        DoUpdate();
                    }

                    // game is not in focus, give CPU time to other apps by waiting for messages instead of 'peeking'
                    SDL_Event wait_e;
                    while (!gameFocused && SDL_WaitEvent(&wait_e)) {
                        if (wait_e.type == SDL_WINDOWEVENT && wait_e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                            gameFocused = true;
                        }
                    }
                }
            }

            deleteGame();
        }

        CleanUp();

        return 0;
#ifdef NDEBUG
    } catch (const std::exception& error) {
        CleanUp();

        std::string e = "Caught exception: ";
        e += error.what();

        LOG(e);

        std::string box = error.what();
#ifdef _WIN32
        fprintf(stderr, "LugaruEX: thrown at\n%s\n", exThrowSite(true).c_str());
        fflush(stderr);
        box += "\n\nLugaruEX " EX_VERSION_STRING ", thrown at:\n" + exThrowSite(false) +
               "\n\nPlease send %APPDATA%\\LugaruEX\\LugaruEX.log (or LugaruEX.prev.log if the game was started again).";
#endif
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "LugaruEX error", box.c_str(), NULL);

        return -1;
    }
#endif
}
