/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "EX/ExCapture.hpp"

#include "EX/ExConfig.hpp"
#include "Game.hpp"
#include "Graphic/Text.hpp"
#include "Utils/Folders.hpp"

#include <SDL.h>
#include <png.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "ole32.lib")
#endif
#else
#include <csignal>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern SDL_Window* sdlwindow;

namespace EX
{
// ------------------------------------------------------------------ folder, names, notice
static bool makeDir(const std::string& d)
{
#ifdef _WIN32
    _mkdir(d.c_str());
#else
    mkdir(d.c_str(), 0755);
#endif
    const std::string probe = d + "/.write-test";
    FILE* f = fopen(probe.c_str(), "wb");
    if (!f) {
        return false;
    }
    fclose(f);
    remove(probe.c_str());
    return true;
}

// LugaruEX/Screenshots & Recording (next to LugaruEX.ini); this PC's user folder if that is read-only
static std::string captureDir()
{
    static std::string dir;
    if (dir.empty()) {
        dir = ExFolder() + "Screenshots & Recording";
        if (!makeDir(dir)) {
            dir = Folders::getUserDataPath() + "/Screenshots & Recording";
            makeDir(dir);
        }
    }
    return dir;
}

static std::string freshName(const char* ext)
{
    const time_t now = time(nullptr);
    tm lt;
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H-%M-%S", &lt);
    const std::string base = captureDir() + "/Lugaru EX " + stamp;
    std::string name = base + ext;
    for (int n = 2; n < 100; n++) {
        FILE* f = fopen(name.c_str(), "rb");
        if (!f) {
            break;
        }
        fclose(f);
        name = base + " (" + std::to_string(n) + ")" + ext;
    }
    return name;
}

static std::string g_note;
static Uint32 g_noteAt = 0;

static void note(const char* s)
{
    g_note = s;
    g_noteAt = SDL_GetTicks();
    printf("LugaruEX: %s\n", s);
}

// top-right, the game's font, fading like the lighting name; drawn after the capture
static void drawNote(int W, int H)
{
    if (!g_noteAt || !Game::text) {
        return;
    }
    const float age = (SDL_GetTicks() - g_noteAt) / 1000.f;
    const float hold = 1.6f, fade = 0.9f;
    if (age > hold + fade) {
        g_noteAt = 0;
        return;
    }
    const float a = age < hold ? 1.f : 1.f - (age - hold) / fade;
    const float size = 1.5f * H / 768.f;
    const float margin = 20.f * H / 768.f;
    const float x = W - margin - g_note.size() * 10.f * size;
    const float y = H - margin - 16.f * size;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glViewport(0, 0, W, H);
    glDisable(GL_SCISSOR_TEST); // (split screen draws each view inside a scissor box)
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    Game::text->glPrintOutlined(1, 1, 1, a, x, y, g_note, 1, size, (float)W, (float)H);
    glPopAttrib();
}

// ------------------------------------------------------------------ screenshot
static std::atomic<int> g_writing{ 0 }; // screenshots still being written

static void writePng(const std::string& path, const std::vector<unsigned char>& rgb, int W, int H)
{
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) {
        fprintf(stderr, "LugaruEX: could not write %s\n", path.c_str());
        return;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    std::vector<png_bytep> rows(H);
    if (png && info && !setjmp(png_jmpbuf(png))) {
        png_init_io(png, fp);
        png_set_compression_level(png, 3); // quick, still small enough
        png_set_IHDR(png, info, W, H, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        png_write_info(png, info);
        for (int i = 0; i < H; i++) {
            rows[i] = (png_bytep)&rgb[(size_t)(H - 1 - i) * W * 3]; // OpenGL rows start at the bottom
        }
        png_write_image(png, rows.data());
        png_write_end(png, nullptr);
    }
    png_destroy_write_struct(&png, &info);
    fclose(fp);
}

static void takeScreenshot(int W, int H)
{
    std::vector<unsigned char> rgb((size_t)W * H * 3);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glPopClientAttrib();
    const std::string path = freshName(".png");
    g_writing++;
    std::thread([rgb = std::move(rgb), W, H, path]() {
        writePng(path, rgb, W, H);
        g_writing--;
    }).detach();
    note("Screenshot saved");
}

// ------------------------------------------------------------------ GL for recording
static PFNGLGENFRAMEBUFFERSPROC c_glGenFramebuffers;
static PFNGLDELETEFRAMEBUFFERSPROC c_glDeleteFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC c_glBindFramebuffer;
static PFNGLFRAMEBUFFERTEXTURE2DPROC c_glFramebufferTexture2D;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC c_glCheckFramebufferStatus;
static PFNGLBLITFRAMEBUFFERPROC c_glBlitFramebuffer;
static PFNGLGENBUFFERSPROC c_glGenBuffers;
static PFNGLDELETEBUFFERSPROC c_glDeleteBuffers;
static PFNGLBINDBUFFERPROC c_glBindBuffer;
static PFNGLBUFFERDATAPROC c_glBufferData;
static PFNGLMAPBUFFERPROC c_glMapBuffer;
static PFNGLUNMAPBUFFERPROC c_glUnmapBuffer;
static PFNGLCREATESHADERPROC c_glCreateShader;
static PFNGLSHADERSOURCEPROC c_glShaderSource;
static PFNGLCOMPILESHADERPROC c_glCompileShader;
static PFNGLGETSHADERIVPROC c_glGetShaderiv;
static PFNGLCREATEPROGRAMPROC c_glCreateProgram;
static PFNGLATTACHSHADERPROC c_glAttachShader;
static PFNGLLINKPROGRAMPROC c_glLinkProgram;
static PFNGLGETPROGRAMIVPROC c_glGetProgramiv;
static PFNGLUSEPROGRAMPROC c_glUseProgram;
static PFNGLGETUNIFORMLOCATIONPROC c_glGetUniformLocation;
static PFNGLUNIFORM1IPROC c_glUniform1i;
static PFNGLUNIFORM2FPROC c_glUniform2f;
static PFNGLACTIVETEXTUREPROC c_glActiveTexture;

template <class T>
static bool load(T& fn, const char* name)
{
    fn = (T)SDL_GL_GetProcAddress(name);
    return fn != nullptr;
}

static bool glReady()
{
    static int state = 0; // 0 unknown, 1 ok, -1 missing
    if (state == 0) {
        const bool ok = load(c_glGenFramebuffers, "glGenFramebuffers") && load(c_glDeleteFramebuffers, "glDeleteFramebuffers") &&
                        load(c_glBindFramebuffer, "glBindFramebuffer") && load(c_glFramebufferTexture2D, "glFramebufferTexture2D") &&
                        load(c_glCheckFramebufferStatus, "glCheckFramebufferStatus") && load(c_glBlitFramebuffer, "glBlitFramebuffer") &&
                        load(c_glGenBuffers, "glGenBuffers") && load(c_glDeleteBuffers, "glDeleteBuffers") && load(c_glBindBuffer, "glBindBuffer") &&
                        load(c_glBufferData, "glBufferData") && load(c_glMapBuffer, "glMapBuffer") && load(c_glUnmapBuffer, "glUnmapBuffer") &&
                        load(c_glCreateShader, "glCreateShader") && load(c_glShaderSource, "glShaderSource") &&
                        load(c_glCompileShader, "glCompileShader") && load(c_glGetShaderiv, "glGetShaderiv") &&
                        load(c_glCreateProgram, "glCreateProgram") && load(c_glAttachShader, "glAttachShader") &&
                        load(c_glLinkProgram, "glLinkProgram") && load(c_glGetProgramiv, "glGetProgramiv") &&
                        load(c_glUseProgram, "glUseProgram") && load(c_glGetUniformLocation, "glGetUniformLocation") &&
                        load(c_glUniform1i, "glUniform1i") && load(c_glUniform2f, "glUniform2f") && load(c_glActiveTexture, "glActiveTexture");
        state = ok ? 1 : -1;
    }
    return state == 1;
}

// RGB -> NV12 (BT.709, video range) in one pass, into a w x h*3/2 single-channel target: the
// luma rows, then the chroma rows (U V U V ..., one pair per 2x2 block, its average). Rows
// come out top-down in memory, as NV12 is stored, though OpenGL counts from the bottom.
static const char* kVS = "#version 120\nvoid main() { gl_Position = gl_Vertex; }\n";
static const char* kFS =
    "#version 120\n"
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uSize;\n"
    "void main() {\n"
    "  vec2 f = floor(gl_FragCoord.xy);\n"
    "  if (f.y < uSize.y) {\n"
    "    vec3 c = texture2D(uSrc, vec2(f.x + 0.5, uSize.y - f.y - 0.5) / uSize).rgb;\n"
    "    gl_FragColor = vec4((16.0 + 219.0 * dot(c, vec3(0.2126, 0.7152, 0.0722))) / 255.0);\n"
    "  } else {\n"
    "    float cy = f.y - uSize.y, cx = floor(f.x * 0.5);\n"
    "    vec3 c = texture2D(uSrc, vec2(cx * 2.0 + 1.0, uSize.y - cy * 2.0 - 1.0) / uSize).rgb;  // the shared corner: bilinear = the block's mean\n"
    "    float u = 128.0 + 224.0 * dot(c, vec3(-0.1146, -0.3854, 0.5));\n"
    "    float v = 128.0 + 224.0 * dot(c, vec3(0.5, -0.4542, -0.0458));\n"
    "    gl_FragColor = vec4((mod(f.x, 2.0) < 0.5 ? u : v) / 255.0);\n"
    "  }\n"
    "}\n";

static const int kPbos = 3; // read back three captures late: the GPU has long finished by then
static GLuint g_srcTex = 0, g_srcFbo = 0, g_yuvTex = 0, g_yuvFbo = 0, g_prog = 0;
static GLuint g_pbo[kPbos] = {};
static long long g_pboSlot[kPbos];
static int g_pboNext = 0;
static int g_rw = 0, g_rh = 0; // recorded size (even)

static GLuint compile(GLenum type, const char* src)
{
    GLuint s = c_glCreateShader(type);
    c_glShaderSource(s, 1, &src, nullptr);
    c_glCompileShader(s);
    GLint ok = 0;
    c_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    return ok ? s : 0;
}

static void freeGL()
{
    if (g_srcFbo) {
        c_glDeleteFramebuffers(1, &g_srcFbo);
    }
    if (g_yuvFbo) {
        c_glDeleteFramebuffers(1, &g_yuvFbo);
    }
    if (g_srcTex) {
        glDeleteTextures(1, &g_srcTex);
    }
    if (g_yuvTex) {
        glDeleteTextures(1, &g_yuvTex);
    }
    for (GLuint& b : g_pbo) {
        if (b) {
            c_glDeleteBuffers(1, &b);
        }
        b = 0;
    }
    g_srcFbo = g_yuvFbo = g_srcTex = g_yuvTex = 0;
}

static GLuint makeTarget(GLint internal, GLenum format, int w, int h, bool linear, GLuint& fbo)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, GL_UNSIGNED_BYTE, nullptr);
    c_glGenFramebuffers(1, &fbo);
    c_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    c_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
    if (c_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        c_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        c_glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &t);
        fbo = 0;
        return 0;
    }
    c_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return t;
}

static bool makeGL(int w, int h)
{
    if (!glReady()) {
        return false;
    }
    if (!g_prog) {
        GLuint vs = compile(GL_VERTEX_SHADER, kVS), fs = compile(GL_FRAGMENT_SHADER, kFS);
        if (!vs || !fs) {
            return false;
        }
        g_prog = c_glCreateProgram();
        c_glAttachShader(g_prog, vs);
        c_glAttachShader(g_prog, fs);
        c_glLinkProgram(g_prog);
        GLint ok = 0;
        c_glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            g_prog = 0;
            return false;
        }
    }
    glPushAttrib(GL_TEXTURE_BIT);
    g_srcTex = makeTarget(GL_RGBA8, GL_RGBA, w, h, true, g_srcFbo);
    g_yuvTex = makeTarget(0x8229 /* GL_R8 */, 0x1903 /* GL_RED */, w, h * 3 / 2, false, g_yuvFbo);
    if (!g_yuvTex) { // no single-channel targets: a colour one, read back as one channel anyway
        g_yuvTex = makeTarget(GL_RGBA8, GL_RGBA, w, h * 3 / 2, false, g_yuvFbo);
    }
    glPopAttrib();
    if (!g_srcTex || !g_yuvTex) {
        freeGL();
        return false;
    }
    c_glGenBuffers(kPbos, g_pbo);
    for (int i = 0; i < kPbos; i++) {
        c_glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pbo[i]);
        c_glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)w * h * 3 / 2, nullptr, GL_STREAM_READ);
        g_pboSlot[i] = -1;
    }
    c_glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    g_pboNext = 0;
    return true;
}

// ------------------------------------------------------------------ encoder queue
struct Item
{
    bool audio;
    long long slot;  // video: frame slot (1/60 s); audio: first sample frame
    std::vector<unsigned char> data;
};
static std::mutex g_qm;
static std::condition_variable g_qcv;
static std::deque<Item> g_q;
static bool g_qEnd = false;
static int g_qVideo = 0;
static std::thread g_worker, g_audio;
static std::atomic<int> g_encState{ 0 }; // 0 opening, 1 writing, -1 failed
static std::atomic<bool> g_audioRun{ false };
static std::vector<std::vector<unsigned char>> g_pool; // spare frame buffers
static const int kFps = 60;
static const int kRate = 48000; // audio: 48 kHz 16-bit stereo
static bool g_rec = false;
static Uint64 g_recStart = 0;
static long long g_lastSlot = -1;
static int g_dropped = 0, g_droppedLate = 0;
static std::string g_recPath;

static void push(Item&& it)
{
    std::lock_guard<std::mutex> l(g_qm);
    if (!it.audio) {
        g_qVideo++;
    }
    g_q.push_back(std::move(it));
    g_qcv.notify_one();
}

#ifdef _WIN32
// ------------------------------------------------------------------ Windows: Media Foundation
template <class T>
static void rel(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

struct Encoder
{
    IMFSinkWriter* w = nullptr;
    DWORD vs = 0, as = 0;
    bool audio = false;

    bool tryOpen(const std::string& path, int W, int H, bool withAudio)
    {
        IMFAttributes* attr = nullptr;
        MFCreateAttributes(&attr, 2);
        attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE); // the graphics card's encoder
        attr->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
        wchar_t wpath[1024];
        MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wpath, 1024);
        HRESULT hr = MFCreateSinkWriterFromURL(wpath, nullptr, attr, &w);
        rel(attr);
        if (FAILED(hr)) {
            return false;
        }
        IMFMediaType *out = nullptr, *in = nullptr;
        const UINT32 bitrate = (UINT32)std::min(60e6, std::max(8e6, (double)W * H * kFps * 0.12));
        MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        out->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(out, MF_MT_FRAME_SIZE, W, H);
        MFSetAttributeRatio(out, MF_MT_FRAME_RATE, kFps, 1);
        MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = w->AddStream(out, &vs);
        rel(out);
        if (SUCCEEDED(hr)) {
            MFCreateMediaType(&in);
            in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
            in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            in->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)W);
            in->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
            in->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
            MFSetAttributeSize(in, MF_MT_FRAME_SIZE, W, H);
            MFSetAttributeRatio(in, MF_MT_FRAME_RATE, kFps, 1);
            MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            hr = w->SetInputMediaType(vs, in, nullptr);
            rel(in);
        }
        if (SUCCEEDED(hr) && withAudio) {
            MFCreateMediaType(&out);
            out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
            out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000); // 192 kbit/s
            hr = w->AddStream(out, &as);
            rel(out);
            if (SUCCEEDED(hr)) {
                MFCreateMediaType(&in);
                in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
                in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
                in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
                in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRate * 4);
                hr = w->SetInputMediaType(as, in, nullptr);
                rel(in);
            }
            audio = SUCCEEDED(hr);
        }
        if (SUCCEEDED(hr)) {
            hr = w->BeginWriting();
        }
        if (FAILED(hr)) {
            rel(w);
            audio = false;
            return false;
        }
        return true;
    }

    bool open(const std::string& path, int W, int H, bool withAudio)
    {
        if (FAILED(MFStartup(MF_VERSION))) {
            return false;
        }
        if (withAudio && tryOpen(path, W, H, true)) {
            return true;
        }
        DeleteFileA(path.c_str());
        return tryOpen(path, W, H, false); // video only (no AAC encoder on this PC)
    }

    void write(DWORD stream, const unsigned char* p, size_t n, LONGLONG t, LONGLONG d)
    {
        IMFMediaBuffer* b = nullptr;
        IMFSample* s = nullptr;
        if (FAILED(MFCreateMemoryBuffer((DWORD)n, &b))) {
            return;
        }
        BYTE* dst = nullptr;
        if (SUCCEEDED(b->Lock(&dst, nullptr, nullptr))) {
            memcpy(dst, p, n);
            b->Unlock();
            b->SetCurrentLength((DWORD)n);
            if (SUCCEEDED(MFCreateSample(&s))) {
                s->AddBuffer(b);
                s->SetSampleTime(t);
                s->SetSampleDuration(d);
                w->WriteSample(stream, s);
            }
        }
        rel(s);
        rel(b);
    }

    void video(const Item& it)
    {
        write(vs, it.data.data(), it.data.size(), it.slot * 10000000LL / kFps, 10000000LL / kFps);
    }

    void sound(const Item& it)
    {
        if (audio) {
            const long long frames = (long long)it.data.size() / 4;
            write(as, it.data.data(), it.data.size(), it.slot * 10000000LL / kRate, frames * 10000000LL / kRate);
        }
    }

    void close()
    {
        if (w) {
            w->Finalize(); // writes the MP4's index: the file plays only after this
            rel(w);
        }
        MFShutdown();
    }
};

// the game's own sound only (Windows 10 2004+): WASAPI process loopback of this process
struct Activated : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
{
    LONG ref = 1;
    HANDLE done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    IAudioClient* client = nullptr;
    STDMETHODIMP QueryInterface(REFIID id, void** out) override
    {
        if (id == __uuidof(IUnknown) || id == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        } else if (id == __uuidof(IAgileObject)) {
            *out = static_cast<IAgileObject*>(this);
        } else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG r = InterlockedDecrement(&ref);
        if (!r) {
            CloseHandle(done);
            delete this;
        }
        return r;
    }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override
    {
        HRESULT hr = E_FAIL;
        IUnknown* u = nullptr;
        if (SUCCEEDED(op->GetActivateResult(&hr, &u)) && SUCCEEDED(hr) && u) {
            u->QueryInterface(__uuidof(IAudioClient), (void**)&client);
        }
        if (u) {
            u->Release();
        }
        SetEvent(done);
        return S_OK;
    }
};

static void audioThread()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    AUDIOCLIENT_ACTIVATION_PARAMS p = {};
    p.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    p.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
    p.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv = {};
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(p);
    pv.blob.pBlobData = (BYTE*)&p;
    Activated* h = new Activated();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* cap = nullptr;
    HANDLE ready = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (SUCCEEDED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv, h, &op)) &&
        WaitForSingleObject(h->done, 3000) == WAIT_OBJECT_0) {
        client = h->client;
        h->client = nullptr;
    }
    WAVEFORMATEX fmt = { WAVE_FORMAT_PCM, 2, (DWORD)kRate, (DWORD)kRate * 4, 4, 16, 0 };
    bool ok = client && SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000, 0, &fmt, nullptr)) &&
              SUCCEEDED(client->SetEventHandle(ready)) && SUCCEEDED(client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap)) &&
              SUCCEEDED(client->Start());
    if (!ok) {
        printf("LugaruEX: recording without sound (this Windows can't capture the game's own audio)\n");
    }
    long long written = 0; // sample frames given to the encoder
    const Uint64 freq = SDL_GetPerformanceFrequency();
    auto pushFrames = [&](const BYTE* data, UINT32 frames, bool silent) {
        Item it;
        it.audio = true;
        it.slot = written;
        it.data.assign((size_t)frames * 4, 0);
        if (!silent && data) {
            memcpy(it.data.data(), data, it.data.size());
        }
        written += frames;
        push(std::move(it));
    };
    while (g_audioRun) { // (no capture: silence on the clock keeps the sound track complete)
        if (ok) {
            WaitForSingleObject(ready, 20); // (short: stopping waits for this thread)
        } else {
            Sleep(50);
        }
        // keep the sound on the clock: silence fills any gap (nothing playing, a late start)
        const long long expected = (long long)((SDL_GetPerformanceCounter() - g_recStart) * (double)kRate / freq);
        if (expected - written > kRate / 10) {
            pushFrames(nullptr, (UINT32)(expected - written), true);
        }
        UINT32 packet = 0;
        while (ok && SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
            pushFrames(data, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
            cap->ReleaseBuffer(frames);
        }
    }
    if (client) {
        client->Stop();
    }
    rel(cap);
    rel(client);
    rel(op);
    h->Release();
    CloseHandle(ready);
    CoUninitialize();
}
#else
// ------------------------------------------------------------------ Linux: ffmpeg, when installed
struct Encoder
{
    FILE* pipe = nullptr;
    long long last = -1;
    std::vector<unsigned char> prev;

    bool open(const std::string& path, int W, int H, bool)
    {
        if (system("command -v ffmpeg >/dev/null 2>&1") != 0) {
            return false;
        }
        signal(SIGPIPE, SIG_IGN);
        std::string quoted = "'";
        for (char c : path) {
            quoted += (c == '\'') ? std::string("'\\''") : std::string(1, c);
        }
        quoted += "'";
        const std::string cmd = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt nv12 -s " + std::to_string(W) + "x" + std::to_string(H) +
                                " -r " + std::to_string(kFps) + " -color_range tv -colorspace bt709 -i - -c:v libx264 -preset veryfast -crf 20" +
                                " -pix_fmt yuv420p -movflags +faststart " + quoted;
        pipe = popen(cmd.c_str(), "w");
        return pipe != nullptr;
    }

    void video(const Item& it)
    {
        // a constant-rate stream: a frame the game skipped repeats the one before
        for (long long s = last + 1; s < it.slot && !prev.empty() && s < last + kFps; s++) {
            fwrite(prev.data(), 1, prev.size(), pipe);
        }
        fwrite(it.data.data(), 1, it.data.size(), pipe);
        last = it.slot;
        prev = it.data;
    }

    void sound(const Item&) {}

    void close()
    {
        if (pipe) {
            pclose(pipe);
            pipe = nullptr;
        }
    }
};
#endif

static void workerThread(std::string path, int W, int H)
{
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    Encoder enc;
    const bool ok = enc.open(path, W, H, true);
    g_encState = ok ? 1 : -1;
    for (;;) {
        Item it;
        {
            std::unique_lock<std::mutex> l(g_qm);
            g_qcv.wait(l, [] { return !g_q.empty() || g_qEnd; });
            if (g_q.empty()) {
                break; // finished and drained
            }
            it = std::move(g_q.front());
            g_q.pop_front();
            if (!it.audio) {
                g_qVideo--;
            }
        }
        if (ok) {
            if (it.audio) {
                enc.sound(it);
            } else {
                enc.video(it);
            }
        }
        if (!it.audio) {
            std::lock_guard<std::mutex> l(g_qm);
            if (g_pool.size() < 6) {
                g_pool.push_back(std::move(it.data)); // reuse the memory for a later frame
            }
        }
    }
    if (ok) {
        enc.close();
    }
#ifdef _WIN32
    CoUninitialize();
#endif
}

// ------------------------------------------------------------------ recording on the main thread
static void harvest(int i)
{
    if (g_pboSlot[i] < 0) {
        return;
    }
    const size_t n = (size_t)g_rw * g_rh * 3 / 2;
    c_glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pbo[i]);
    const unsigned char* p = (const unsigned char*)c_glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (p) {
        bool busy;
        const bool opening = g_encState == 0;
        {
            // the encoder is behind: skip this frame rather than pile up (more room while it
            // is still starting, ~200 MB)
            const int limit = opening ? std::max(10, (int)(200u * 1024 * 1024 / n)) : 10;
            std::lock_guard<std::mutex> l(g_qm);
            busy = g_qVideo > limit;
        }
        if (busy) {
            g_dropped++;
            if (!opening) {
                g_droppedLate++;
            }
        } else {
            Item it;
            it.audio = false;
            it.slot = g_pboSlot[i];
            {
                std::lock_guard<std::mutex> l(g_qm);
                if (!g_pool.empty()) {
                    it.data = std::move(g_pool.back());
                    g_pool.pop_back();
                }
            }
            it.data.resize(n);
            memcpy(it.data.data(), p, n);
            push(std::move(it));
        }
        c_glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    c_glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    g_pboSlot[i] = -1;
}

static void grab(long long slot)
{
    const int i = g_pboNext;
    harvest(i); // the oldest capture: done on the GPU three captures ago
    GLint prog = 0, tex = 0, unit = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    // the finished frame (MSAA resolved by the blit) ...
    c_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    c_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_srcFbo);
    glDisable(GL_SCISSOR_TEST);
    c_glBlitFramebuffer(0, 0, g_rw, g_rh, 0, 0, g_rw, g_rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    // ... converted to NV12 ...
    c_glBindFramebuffer(GL_FRAMEBUFFER, g_yuvFbo);
    glViewport(0, 0, g_rw, g_rh * 3 / 2);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    c_glUseProgram(g_prog);
    c_glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    glBindTexture(GL_TEXTURE_2D, g_srcTex);
    c_glUniform1i(c_glGetUniformLocation(g_prog, "uSrc"), 0);
    c_glUniform2f(c_glGetUniformLocation(g_prog, "uSize"), (float)g_rw, (float)g_rh);
    glBegin(GL_QUADS);
    glVertex2f(-1, -1);
    glVertex2f(1, -1);
    glVertex2f(1, 1);
    glVertex2f(-1, 1);
    glEnd();
    // ... and read back into a pixel buffer without waiting for it
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    c_glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pbo[i]);
    glReadPixels(0, 0, g_rw, g_rh * 3 / 2, 0x1903 /* GL_RED */, GL_UNSIGNED_BYTE, nullptr);
    c_glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    g_pboSlot[i] = slot;
    g_pboNext = (i + 1) % kPbos;
    // put everything back as the game had it
    glBindTexture(GL_TEXTURE_2D, tex);
    c_glActiveTexture(unit);
    c_glUseProgram(prog);
    c_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPopClientAttrib();
    glPopAttrib();
    glReadBuffer(GL_BACK);
}

static void stopRecording(const char* why)
{
    if (!g_rec) {
        return;
    }
    for (int k = 0; k < kPbos; k++) { // the last captures still on their way back
        harvest((g_pboNext + k) % kPbos);
    }
    g_rec = false;
    g_audioRun = false;
    if (g_audio.joinable()) {
        g_audio.join();
    }
    {
        std::lock_guard<std::mutex> l(g_qm);
        g_qEnd = true;
        g_qcv.notify_all();
    }
    // the worker finishes the file by itself (joined before the next recording or at exit)
    freeGL();
    if (g_dropped) {
        printf("LugaruEX: recording skipped %d frames (the encoder was busy; %d after it started)\n", g_dropped, g_droppedLate);
    }
    printf("LugaruEX: recording %s\n", g_recPath.c_str());
    note(why);
}

static void startRecording(int W, int H)
{
    if (g_worker.joinable()) {
        g_worker.join(); // the previous recording's file is finished
    }
    g_rw = W & ~1;
    g_rh = H & ~1;
    if (g_rw < 16 || g_rh < 16 || !makeGL(g_rw, g_rh)) {
        note("Recording unavailable");
        return;
    }
    g_recPath = freshName(".mp4");
    {
        std::lock_guard<std::mutex> l(g_qm);
        g_q.clear();
        g_qEnd = false;
        g_qVideo = 0;
    }
    g_encState = 0;
    g_dropped = g_droppedLate = 0;
    g_lastSlot = -1;
    g_recStart = SDL_GetPerformanceCounter();
    g_worker = std::thread(workerThread, g_recPath, g_rw, g_rh);
#ifdef _WIN32
    g_audioRun = true;
    g_audio = std::thread(audioThread);
#endif
    g_rec = true;
    note("Recording started");
}

bool CaptureRecording()
{
    return g_rec;
}

void CaptureFrame()
{
    int W = 0, H = 0;
    SDL_GL_GetDrawableSize(sdlwindow, &W, &H);
    if (W <= 0 || H <= 0) {
        return;
    }
    // F11 / F12 (and test hooks LUGARUEX_TEST_SHOT=s, LUGARUEX_TEST_REC=start,stop seconds)
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    static bool prev11 = false, prev12 = false;
    const bool f11 = keys[SDL_SCANCODE_F11] != 0, f12 = keys[SDL_SCANCODE_F12] != 0;
    bool shot = f12 && !prev12, rec = f11 && !prev11;
    prev11 = f11;
    prev12 = f12;
    {
        const float t = SDL_GetTicks() / 1000.f;
        static const char* testShot = getenv("LUGARUEX_TEST_SHOT");
        static bool testShotDone = false;
        if (testShot && !testShotDone && t > atof(testShot)) {
            testShotDone = shot = true;
        }
        static const char* testRec = getenv("LUGARUEX_TEST_REC");
        static int testRecStep = 0;
        if (testRec) {
            const char* comma = strchr(testRec, ',');
            if (testRecStep == 0 && t > atof(testRec)) {
                testRecStep = 1;
                rec = !g_rec;
            } else if (testRecStep == 1 && comma && t > atof(comma + 1)) {
                testRecStep = 2;
                rec = g_rec;
            }
        }
    }
    if (shot) {
        takeScreenshot(W, H);
    }
    if (rec) {
        if (g_rec) {
            stopRecording("Recording stopped");
        } else {
            startRecording(W, H);
        }
    }
    if (g_rec) {
        if (g_encState == -1) {
            stopRecording("Recording unavailable");
#ifndef _WIN32
            note("Recording needs ffmpeg");
#endif
        } else if ((W & ~1) != g_rw || (H & ~1) != g_rh) {
            stopRecording("Recording stopped"); // the window changed size
        } else {
            const long long slot = (long long)((SDL_GetPerformanceCounter() - g_recStart) * (double)kFps / SDL_GetPerformanceFrequency());
            if (slot > g_lastSlot) { // at most 60 a second, whatever the frame rate
                g_lastSlot = slot;
                grab(slot);
            }
        }
    }
    drawNote(W, H);
}

void CaptureShutdown()
{
    stopRecording("Recording stopped");
    if (g_worker.joinable()) {
        g_worker.join();
    }
    for (int i = 0; i < 50 && g_writing > 0; i++) { // a screenshot still being written
        SDL_Delay(100);
    }
}
}
