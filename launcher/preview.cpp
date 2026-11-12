/*
LugaruEX launcher - animated 3D character preview window (Windows).

The model, skinning and drawing are in preview_core.cpp (shared with the SDL
launcher); this file gives it an OpenGL child window, a timer and mouse input,
and reads the skin image with WIC. Drag to rotate, wheel to zoom.

Copyright (C) 2026 - LugaruEX contributors. GPLv2 or later.
*/

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "preview.h"
#include "preview_core.h"

#include <GL/gl.h>
#include <wincodec.h>

#include <vector>

#pragma comment(lib, "opengl32.lib")

namespace
{
struct Win
{
    HDC dc = NULL;
    HGLRC rc = NULL;
    std::string model;
    std::wstring skin;
    LARGE_INTEGER last = {};
};
static Win g;

static std::string narrowUtf8(const std::wstring& w)
{
    if (w.empty()) {
        return "";
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

static void loadTexture(const std::wstring& path)
{
    IWICImagingFactory* fac = NULL;
    if (path.empty() || FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac)))) {
        PreviewCore_SetTexture(nullptr, 0, 0);
        return;
    }
    IWICBitmapDecoder* dec = NULL;
    IWICBitmapFrameDecode* fr = NULL;
    IWICFormatConverter* conv = NULL;
    std::vector<BYTE> px;
    UINT w = 0, h = 0;
    if (SUCCEEDED(fac->CreateDecoderFromFilename(path.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(fac->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(fr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
        conv->GetSize(&w, &h);
        px.resize((size_t)w * h * 4);
        if (FAILED(conv->CopyPixels(NULL, w * 4, (UINT)px.size(), px.data()))) {
            px.clear();
        }
    }
    for (IUnknown* u : { (IUnknown*)conv, (IUnknown*)fr, (IUnknown*)dec, (IUnknown*)fac }) {
        if (u) {
            u->Release();
        }
    }
    PreviewCore_SetTexture(px.empty() ? nullptr : px.data(), (int)w, (int)h);
}

static void render(HWND wnd)
{
    RECT rc;
    GetClientRect(wnd, &rc);
    PreviewCore_Render(0, 0, rc.right, rc.bottom);
    SwapBuffers(g.dc);
}

static LRESULT CALLBACK PreviewProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_CREATE: {
            g.dc = GetDC(wnd);
            PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
            pfd.cDepthBits = 24;
            pfd.iLayerType = PFD_MAIN_PLANE;
            int pf = ChoosePixelFormat(g.dc, &pfd);
            if (pf && SetPixelFormat(g.dc, pf, &pfd)) {
                g.rc = wglCreateContext(g.dc);
                if (g.rc) {
                    wglMakeCurrent(g.dc, g.rc);
                }
            }
            SetTimer(wnd, 1, 15, NULL);
            return 0;
        }
        case WM_TIMER:
            if (IsWindowVisible(wnd)) {
                LARGE_INTEGER now, freq;
                QueryPerformanceCounter(&now);
                QueryPerformanceFrequency(&freq);
                const double dt = g.last.QuadPart ? (double)(now.QuadPart - g.last.QuadPart) / freq.QuadPart : 0;
                g.last = now;
                PreviewCore_Advance(dt, GetTickCount());
                InvalidateRect(wnd, NULL, FALSE);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(wnd, &ps);
            if (g.rc) {
                wglMakeCurrent(g.dc, g.rc);
                render(wnd);
            }
            EndPaint(wnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            PreviewCore_DragBegin((short)LOWORD(lp), (short)HIWORD(lp), GetTickCount());
            SetCapture(wnd);
            return 0;
        case WM_MOUSEMOVE:
            PreviewCore_DragMove((short)LOWORD(lp), (short)HIWORD(lp), GetTickCount());
            return 0;
        case WM_LBUTTONUP:
            PreviewCore_DragEnd(GetTickCount());
            ReleaseCapture();
            return 0;
        case WM_MOUSEWHEEL:
            PreviewCore_Wheel(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1, GetTickCount());
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            return TRUE;
        case WM_DESTROY:
            KillTimer(wnd, 1);
            if (g.rc) {
                PreviewCore_FreeGL();
                wglMakeCurrent(NULL, NULL);
                wglDeleteContext(g.rc);
                g.rc = NULL;
            }
            ReleaseDC(wnd, g.dc);
            return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}
} // namespace

void Preview_Register(HINSTANCE inst, const std::wstring& gameDir)
{
    PreviewCore_SetDataDir(narrowUtf8(gameDir) + "Data/");
    WNDCLASSW wc = {};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PreviewProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_SIZEALL);
    wc.lpszClassName = L"LugaruEXPreview";
    RegisterClassW(&wc);
}

void Preview_Set(HWND wnd, const std::string& model, const std::wstring& skinFile, bool dimmed)
{
    if (!g.rc) {
        return;
    }
    wglMakeCurrent(g.dc, g.rc);
    PreviewCore_SetModel(model);
    if (skinFile != g.skin) {
        g.skin = skinFile;
        loadTexture(skinFile);
    }
    PreviewCore_SetDimmed(dimmed);
    InvalidateRect(wnd, NULL, FALSE);
}
