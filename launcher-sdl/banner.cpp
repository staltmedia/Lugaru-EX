/*
LugaruEX launcher for Linux - banner (a port of BuildBanner in launcher/launcher.cpp:
the same steps, with stb_truetype for the lettering instead of GDI).

The banner is composed at run time from the user's own game files
(Data/Textures/Eyelid.png and Data/Textures/Lugaru.png); no game art is
embedded in this program. GPLv2 or later.
*/

#include "banner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

typedef unsigned char BYTE;

// ---------------------------------------------------------------- fonts
#ifndef _WIN32
// fontconfig's best match for a family: "family|file", or "" when fc-match is missing
static std::string fcMatch(const std::string& pattern)
{
    const std::string cmd = "fc-match -f '%{family}|%{file}' '" + pattern + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) {
        return "";
    }
    char buf[1024] = {};
    const size_t n = fread(buf, 1, sizeof(buf) - 1, p);
    pclose(p);
    return std::string(buf, n);
}
#endif

std::string FindFontFile(bool serif)
{
#ifdef _WIN32
    const char* win = getenv("WINDIR");
    const std::string dir = std::string(win ? win : "C:/Windows") + "/Fonts/";
    const char* names[] = { "TrajanPro3-Regular.otf", "TrajanPro-Regular.otf", "PER_____.TTF", "sylfaen.ttf", "pala.ttf", "times.ttf" };
    const char* sans[] = { "segoeui.ttf", "tahoma.ttf", "arial.ttf" };
    for (const char* n : serif ? std::vector<const char*>(std::begin(names), std::end(names)) : std::vector<const char*>(std::begin(sans), std::end(sans))) {
        if (FileExists(dir + n)) {
            return dir + n;
        }
    }
    return "";
#else
    // classical capitals first (as close to the LUGARU letters as the system has), then any serif
    const char* serifs[] = { "Trajan Pro 3", "Trajan Pro", "Cinzel", "Perpetua Titling MT", "Sylfaen", "Palatino Linotype",
                             "P052", "URW Palladio L", "TeX Gyre Pagella", "Liberation Serif", "DejaVu Serif", "Noto Serif" };
    const char* sans[] = { "Noto Sans", "Cantarell", "Ubuntu", "DejaVu Sans", "Liberation Sans" };
    std::vector<const char*> want = serif ? std::vector<const char*>(std::begin(serifs), std::end(serifs))
                                          : std::vector<const char*>(std::begin(sans), std::end(sans));
    for (const char* f : want) {
        const std::string r = fcMatch(f);
        const size_t bar = r.find('|');
        if (bar != std::string::npos && Lower(r.substr(0, bar)).find(Lower(f)) != std::string::npos && FileExists(r.substr(bar + 1))) {
            return r.substr(bar + 1);
        }
    }
    const std::string any = fcMatch(serif ? "serif" : "sans-serif");
    const size_t bar = any.find('|');
    if (bar != std::string::npos && FileExists(any.substr(bar + 1))) {
        return any.substr(bar + 1);
    }
    for (const char* p : { "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf", "/usr/share/fonts/TTF/DejaVuSerif.ttf",
                           "/usr/share/fonts/dejavu/DejaVuSerif.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                           "/usr/share/fonts/TTF/DejaVuSans.ttf" }) {
        if (FileExists(p)) {
            return p;
        }
    }
    return "";
#endif
}

namespace
{
struct Font
{
    std::vector<unsigned char> data;
    stbtt_fontinfo info;
    bool ok = false;
};

static Font& exFont()
{
    static Font f;
    static bool tried = false;
    if (tried) {
        return f;
    }
    tried = true;
    const std::string path = FindFontFile(true);
    if (FILE* fp = fopen(path.c_str(), "rb")) {
        fseek(fp, 0, SEEK_END);
        const long n = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (n > 0) {
            f.data.resize((size_t)n);
            if (fread(f.data.data(), 1, (size_t)n, fp) == (size_t)n) {
                const int off = stbtt_GetFontOffsetForIndex(f.data.data(), 0);
                f.ok = off >= 0 && stbtt_InitFont(&f.info, f.data.data(), off);
            }
        }
        fclose(fp);
    }
    printf("LugaruEX launcher: EX lettering font %s\n", f.ok ? path.c_str() : "(none)");
    return f;
}

// coverage (0..1) of "EX" at a font height, 4x supersampled, cropped to the glyphs plus
// `pad` pixels; strokes thickened to stemRatio x the glyph height
struct ExMask
{
    int w = 0, h = 0, gw = 0, gh = 0, pad = 0;
    std::vector<float> a;
    float at(int x, int y) const { return (x < 0 || y < 0 || x >= w || y >= h) ? 0.f : a[(size_t)y * w + x]; }
};

static ExMask RenderExMask(float fontPx, int pad, float stemRatio)
{
    ExMask m;
    Font& font = exFont();
    if (!font.ok) {
        return m;
    }
    const int SS = 4;
    const int MW = (int)(fontPx * 2.4f * SS) + 16, MH = (int)(fontPx * 1.6f * SS) + 16;
    std::vector<BYTE> big((size_t)MW * MH, 0);
    const float scale = stbtt_ScaleForMappingEmToPixels(&font.info, fontPx * SS);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&font.info, &asc, &desc, &gap);
    const int baseline = 8 + (int)lroundf(asc * scale);
    float x = 8.f * SS;
    const int text[2] = { 'E', 'X' };
    for (int i = 0; i < 2; i++) {
        int adv = 0, lsb = 0, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&font.info, text[i], &adv, &lsb);
        stbtt_GetCodepointBitmapBox(&font.info, text[i], scale, scale, &x0, &y0, &x1, &y1);
        const int gx = (int)lroundf(x) + x0, gy = baseline + y0;
        const int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0 && gx >= 0 && gy >= 0 && gx + gw <= MW && gy + gh <= MH) {
            std::vector<BYTE> g((size_t)gw * gh);
            stbtt_MakeCodepointBitmap(&font.info, g.data(), gw, gh, gw, scale, scale, text[i]);
            for (int yy = 0; yy < gh; yy++) {
                for (int xx = 0; xx < gw; xx++) {
                    BYTE& d = big[(size_t)(gy + yy) * MW + gx + xx];
                    d = std::max(d, g[(size_t)yy * gw + xx]);
                }
            }
        }
        x += adv * scale;
        if (i == 0) {
            x += stbtt_GetCodepointKernAdvance(&font.info, text[0], text[1]) * scale;
        }
    }

    int x0 = MW, x1 = -1, y0 = MH, y1 = -1;
    for (int y = 0; y < MH; y++) {
        for (int xx = 0; xx < MW; xx++) {
            if (big[(size_t)y * MW + xx] > 100) {
                x0 = std::min(x0, xx), x1 = std::max(x1, xx), y0 = std::min(y0, y), y1 = std::max(y1, y);
            }
        }
    }
    if (x1 < x0) {
        return m;
    }
    // thicken: measure the E's stem (first run in the middle row) and grow every stroke
    int stem = 0;
    for (int xx = x0, ym = (y0 + y1) / 2; xx <= x1; xx++) {
        if (big[(size_t)ym * MW + xx] > 127) {
            stem++;
        } else if (stem) {
            break;
        }
    }
    const int grow = std::max(0, (int)lroundf((stemRatio * (y1 - y0 + 1) - stem) / 2));
    if (grow > 0) {
        std::vector<BYTE> tmp(big.size());
        for (int y = 0; y < MH; y++) { // horizontal max
            for (int xx = 0; xx < MW; xx++) {
                BYTE v = 0;
                for (int k = std::max(0, xx - grow); k <= std::min(MW - 1, xx + grow); k++) {
                    v = std::max(v, big[(size_t)y * MW + k]);
                }
                tmp[(size_t)y * MW + xx] = v;
            }
        }
        for (int y = 0; y < MH; y++) { // vertical max
            for (int xx = 0; xx < MW; xx++) {
                BYTE v = 0;
                for (int k = std::max(0, y - grow); k <= std::min(MH - 1, y + grow); k++) {
                    v = std::max(v, tmp[(size_t)k * MW + xx]);
                }
                big[(size_t)y * MW + xx] = v;
            }
        }
        x0 = std::max(0, x0 - grow), x1 = std::min(MW - 1, x1 + grow), y0 = std::max(0, y0 - grow), y1 = std::min(MH - 1, y1 + grow);
    }
    // down to 1x, glyph box from x0/y0, plus the pad
    m.pad = pad;
    m.gw = (x1 - x0 + SS) / SS;
    m.gh = (y1 - y0 + SS) / SS;
    m.w = m.gw + 2 * pad;
    m.h = m.gh + 2 * pad;
    m.a.assign((size_t)m.w * m.h, 0.f);
    for (int y = 0; y < m.gh; y++) {
        for (int xx = 0; xx < m.gw; xx++) {
            int s = 0;
            for (int sy = 0; sy < SS; sy++) {
                for (int sx = 0; sx < SS; sx++) {
                    const int bx = x0 + xx * SS + sx, by = y0 + y * SS + sy;
                    s += (bx < MW && by < MH) ? big[(size_t)by * MW + bx] : 0;
                }
            }
            m.a[(size_t)(y + pad) * m.w + (xx + pad)] = s / (255.f * SS * SS);
        }
    }
    return m;
}

static std::vector<float> BoxBlur(const std::vector<float>& src, int w, int h, int r)
{
    std::vector<float> a = src, b(src.size());
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float s = 0;
                for (int k = -r; k <= r; k++) {
                    const int xx = std::max(0, std::min(w - 1, x + k));
                    s += a[(size_t)y * w + xx];
                }
                b[(size_t)y * w + x] = s / (2 * r + 1);
            }
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float s = 0;
                for (int k = -r; k <= r; k++) {
                    const int yy = std::max(0, std::min(h - 1, y + k));
                    s += b[(size_t)yy * w + x];
                }
                a[(size_t)y * w + x] = s / (2 * r + 1);
            }
        }
    }
    return a;
}

// "EX" in the style of the LUGARU letters (see launcher.cpp PaintEX for the details)
template <typename Put>
static void PaintEX(int capH, const float (*grad)[3], int x, int y, Put put, int* outW = nullptr, int* outH = nullptr)
{
    capH = std::max(8, capH);
    float fontPx = capH / 0.66f;
    ExMask probe = RenderExMask(fontPx, 0, 0.f);
    if (probe.gh > 0) {
        fontPx *= (float)capH / probe.gh;
    }
    const int pad = std::max(3, capH / 8);
    ExMask m = RenderExMask(fontPx, pad, 0.105f); // LUGARU's stems are ~1/10 of its height
    if (m.gh <= 0) {
        return;
    }
    if (outW) {
        *outW = m.gw;
    }
    if (outH) {
        *outH = m.gh;
    }
    const int sr = std::max(1, capH / 30);
    const std::vector<float> shadow = BoxBlur(m.a, m.w, m.h, sr);
    const int so = std::max(1, capH / 28);
    for (int my = 0; my < m.h; my++) {
        for (int mx = 0; mx < m.w; mx++) {
            const float a = shadow[(size_t)my * m.w + mx];
            if (a > 0.004f) {
                put(x + mx - m.pad + so, y + my - m.pad + so, 0.f, 0.f, 0.f, a * 0.75f);
            }
        }
    }
    const int br = std::max(1, (int)lroundf(capH * 0.03f));
    const std::vector<float> hgt = BoxBlur(m.a, m.w, m.h, br);
    auto H = [&](int px, int py) { return (px < 0 || py < 0 || px >= m.w || py >= m.h) ? 0.f : hgt[(size_t)py * m.w + px]; };
    const float k = capH * 0.07f;
    const float L[3] = { -0.28f, -0.78f, 0.56f };
    for (int my = 0; my < m.h; my++) {
        const int gy = my - m.pad;
        const int t = std::max(0, std::min(63, gy * 64 / std::max(1, m.gh)));
        const float* c = grad ? grad[t] : nullptr;
        const float base[3] = { c ? c[0] : 225.f, c ? c[1] : 20.f + 40.f * t / 64.f, c ? c[2] : 2.f };
        for (int mx = 0; mx < m.w; mx++) {
            const float a = m.at(mx, my);
            if (a <= 0.004f) {
                continue;
            }
            float nx = -(H(mx + 1, my) - H(mx - 1, my)) * k, ny = -(H(mx, my + 1) - H(mx, my - 1)) * k, nz = 1.f;
            const float nl = sqrtf(nx * nx + ny * ny + nz * nz);
            nx /= nl, ny /= nl, nz /= nl;
            const float diff = nx * L[0] + ny * L[1] + nz * L[2];
            float r = base[0] * (0.74f + 0.36f * diff), g = base[1] * (0.70f + 0.42f * diff), b = base[2] * (0.7f + 0.4f * diff);
            const float lip = std::min(1.f, powf(std::max(0.f, ny), 1.5f) * 0.95f + 0.2f * std::max(0.f, (float)gy / m.gh - 0.88f) / 0.12f);
            r = r + (255.f - r) * lip;
            g = g + (150.f - g) * lip;
            b = b + (40.f - b) * lip;
            const float hx = L[0], hy = L[1], hz = L[2] + 1.f, hl = sqrtf(hx * hx + hy * hy + hz * hz);
            const float spec = powf(std::max(0.f, (nx * hx + ny * hy + nz * hz) / hl), 40.f) * 0.32f;
            r += 255.f * spec, g += 200.f * spec, b += 140.f * spec;
            put(x + mx - m.pad, y + my - m.pad, std::min(255.f, r), std::min(255.f, g), std::min(255.f, b), std::min(1.f, a));
        }
    }
}

// premultiplied BGRA image, like the Windows launcher's (so the pixel rules read the same)
struct Bgra
{
    int w = 0, h = 0;
    std::vector<BYTE> px;
};

static bool loadBgra(const std::string& path, Bgra& out)
{
    Image im;
    if (!ReadImage(path, im)) {
        return false;
    }
    out.w = im.w;
    out.h = im.h;
    out.px.resize(im.px.size());
    for (size_t i = 0; i < im.px.size(); i += 4) {
        const int a = im.px[i + 3];
        out.px[i + 0] = (BYTE)(im.px[i + 2] * a / 255);
        out.px[i + 1] = (BYTE)(im.px[i + 1] * a / 255);
        out.px[i + 2] = (BYTE)(im.px[i + 0] * a / 255);
        out.px[i + 3] = (BYTE)a;
    }
    return true;
}

// area-average of src over [x0,x1) x [y0,y1) (4x4 bilinear taps), premultiplied
static void sampleArea(const Bgra& s, float x0, float y0, float x1, float y1, float out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    const int N = 4;
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            const float fx = x0 + (x1 - x0) * (i + .5f) / N - .5f, fy = y0 + (y1 - y0) * (j + .5f) / N - .5f;
            const int ix = (int)floorf(fx), iy = (int)floorf(fy);
            const float tx = fx - ix, ty = fy - iy;
            for (int k = 0; k < 4; k++) {
                auto P = [&](int x, int y) {
                    x = std::max(0, std::min(s.w - 1, x));
                    y = std::max(0, std::min(s.h - 1, y));
                    return (float)s.px[((size_t)y * s.w + x) * 4 + k];
                };
                const float v = (P(ix, iy) * (1 - tx) + P(ix + 1, iy) * tx) * (1 - ty) + (P(ix, iy + 1) * (1 - tx) + P(ix + 1, iy + 1) * tx) * ty;
                out[k] += v / (N * N);
            }
        }
    }
}
} // namespace

void BuildBanner(const std::string& gameDir, const std::string& exDir, int W, int H, Image& result)
{
    Bgra dst;
    dst.w = W;
    dst.h = H;
    dst.px.resize((size_t)W * H * 4);
    BYTE* bits = dst.px.data();
    // dark red-black gradient fallback
    for (int y = 0; y < H; y++) {
        BYTE* row = bits + (size_t)y * W * 4;
        for (int x = 0; x < W; x++) {
            row[x * 4 + 0] = 8;
            row[x * 4 + 1] = 8;
            row[x * 4 + 2] = (BYTE)(20 + 30 * y / H);
            row[x * 4 + 3] = 255;
        }
    }

    const std::string tex = gameDir + "Data/Textures";
    Bgra eye, logo;
    const std::string eyeFile = FindFileCI(tex, "Eyelid.png"), logoFile = FindFileCI(tex, "Lugaru.png");
    if (!eyeFile.empty() && loadBgra(eyeFile, eye) && eye.w > 0) {
        // scale to cover, centred
        const double s = std::max((double)W / eye.w, (double)H / eye.h);
        const double sw = W / s, sh = H / s, ox = (eye.w - sw) / 2, oy = (eye.h - sh) / 2;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float c[4];
                sampleArea(eye, (float)(ox + x / s), (float)(oy + y / s), (float)(ox + (x + 1) / s), (float)(oy + (y + 1) / s), c);
                BYTE* d = &bits[((size_t)y * W + x) * 4];
                d[0] = (BYTE)c[0], d[1] = (BYTE)c[1], d[2] = (BYTE)c[2], d[3] = 255;
            }
        }
    }
    if (!logoFile.empty() && loadBgra(logoFile, logo) && logo.w > 0) {
        // The logo is drawn on black without an alpha channel: derive alpha from
        // brightness (colours are then already premultiplied over black).
        bool opaque = true;
        for (size_t i = 3; i < logo.px.size(); i += 4) {
            if (logo.px[i] != 255) {
                opaque = false;
                break;
            }
        }
        if (opaque) {
            for (size_t i = 0; i < logo.px.size(); i += 4) {
                BYTE* p = &logo.px[i];
                p[3] = std::max(p[0], std::max(p[1], p[2]));
            }
        }
        auto px = [&](int x, int y) { return &logo.px[((size_t)y * logo.w + x) * 4]; };
        int hdL = logo.w, hdR = 0, hdT = logo.h, hdB = 0;
        for (int y = 0; y < logo.h; y++) {
            for (int x = 0; x < logo.w; x++) {
                const BYTE* p = px(x, y);
                if (p[2] > 160 && p[1] > 130 && p[0] > 90) { // cream-white
                    hdL = std::min(hdL, x), hdR = std::max(hdR, x), hdT = std::min(hdT, y), hdB = std::max(hdB, y);
                }
            }
        }
        int redT = logo.h, redB = 0, redL = logo.w, redR = 0;
        auto letter = [](const BYTE* p) { return p[2] > 90 && p[0] < p[2] * 0.45f && p[1] < p[2] * 0.9f; };
        for (int y = 0; y < logo.h; y++) {
            for (int x = 0; x < logo.w; x++) {
                const BYTE* p = px(x, y);
                if (letter(p)) {
                    if (x < hdL - 2) {
                        redT = std::min(redT, y), redB = std::max(redB, y), redL = std::min(redL, x);
                    }
                    if (y < hdT) {
                        redR = std::max(redR, x);
                    }
                }
            }
        }
        const bool found = redB > redT && hdR > hdL && hdB > hdT;
        if (found) {
            const int zx0 = std::max(0, hdL - 10), zx1 = std::min(logo.w - 1, hdR + 6), zy0 = std::max(0, hdT - 3);
            auto inZone = [&](int x, int y) { return x >= zx0 && x <= zx1 && y >= zy0 && y < logo.h; };
            auto lum = [&](int x, int y) { const BYTE* p = px(x, y); return (int)std::max(p[0], std::max(p[1], p[2])); };
            std::vector<char> erased((size_t)logo.w * logo.h, 0);
            for (int y = zy0; y < logo.h; y++) {
                for (int x = zx0; x <= zx1; x++) {
                    BYTE* p = px(x, y);
                    const int mx = std::max(p[0], std::max(p[1], p[2])), mn = std::min(p[0], std::min(p[1], p[2]));
                    if (mx > 12 && (p[0] > p[2] * 0.28f || mx - mn < mx * 0.35f)) {
                        if (mx > 30) {
                            hdB = std::max(hdB, y);
                        }
                        erased[(size_t)y * logo.w + x] = 1;
                        p[0] = p[1] = p[2] = p[3] = 0;
                    }
                }
            }
            auto solid = [&](int x, int y) {
                return x >= 0 && y >= 0 && x < logo.w && y < logo.h && !erased[(size_t)y * logo.w + x] && lum(x, y) > 150;
            };
            std::vector<BYTE> fill(logo.px);
            const int dirs[4][2] = { { 1, 0 }, { 1, 1 }, { 1, -1 }, { 0, 1 } };
            for (int y = zy0; y <= std::min(logo.h - 1, redB + 12); y++) {
                for (int x = zx0; x <= zx1; x++) {
                    if (!erased[(size_t)y * logo.w + x]) {
                        continue;
                    }
                    int best = 99, ax = 0, ay = 0, bx = 0, by = 0, da = 0;
                    for (const auto& d : dirs) {
                        int a = -1, b = -1;
                        for (int s = 1; s <= 9 && a < 0; s++) {
                            if (solid(x - d[0] * s, y - d[1] * s)) {
                                a = s;
                            }
                        }
                        for (int s = 1; s <= 9 && b < 0; s++) {
                            if (solid(x + d[0] * s, y + d[1] * s)) {
                                b = s;
                            }
                        }
                        if (a > 0 && b > 0 && a + b < best) {
                            best = a + b, da = a;
                            ax = x - d[0] * a, ay = y - d[1] * a, bx = x + d[0] * b, by = y + d[1] * b;
                        }
                    }
                    if (best <= 10) {
                        const float t = (float)da / best;
                        const BYTE *pa = px(ax, ay), *pb = px(bx, by);
                        BYTE* q = &fill[((size_t)y * logo.w + x) * 4];
                        for (int c = 0; c < 4; c++) {
                            q[c] = (BYTE)(pa[c] + (pb[c] - pa[c]) * t);
                        }
                    }
                }
            }
            logo.px.swap(fill);
            const int tailB = std::min(logo.h - 1, redB + std::max(4, (redB - redT) / 9));
            std::vector<char> keep((size_t)logo.w * logo.h, 0);
            std::vector<std::pair<int, int>> st;
            for (int y = zy0; y <= tailB; y++) {
                for (int x = zx0; x <= zx1; x++) {
                    if (lum(x, y) <= 25) {
                        continue;
                    }
                    bool edge = false;
                    for (int dy = -1; dy <= 1 && !edge; dy++) {
                        for (int dx = -1; dx <= 1 && !edge; dx++) {
                            const int nx = x + dx, ny = y + dy;
                            edge = nx >= 0 && ny >= 0 && nx < logo.w && ny < logo.h && !inZone(nx, ny) && lum(nx, ny) > 25;
                        }
                    }
                    if (edge) {
                        st.push_back({ x, y });
                    }
                }
            }
            while (!st.empty()) {
                const auto q = st.back();
                st.pop_back();
                const int x = q.first, y = q.second;
                if (!inZone(x, y) || y > tailB) {
                    continue;
                }
                char& k = keep[(size_t)y * logo.w + x];
                if (k || lum(x, y) <= 25) {
                    continue;
                }
                k = 1;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx || dy) {
                            st.push_back({ x + dx, y + dy });
                        }
                    }
                }
            }
            for (int y = zy0; y < logo.h; y++) {
                for (int x = zx0; x <= zx1; x++) {
                    if (!keep[(size_t)y * logo.w + x]) {
                        BYTE* p = px(x, y);
                        p[0] = p[1] = p[2] = p[3] = 0;
                    }
                }
            }
        }
        float grad[64][3] = {};
        if (found) {
            bool valid[64] = {};
            for (int t = 0; t < 64; t++) {
                const int y0 = redT + (redB - redT) * t / 64, y1 = std::max(y0 + 1, redT + (redB - redT) * (t + 1) / 64);
                double s[3] = {}, n = 0;
                for (int y = y0; y < y1; y++) {
                    for (int x = 0; x < hdL - 2; x++) {
                        const BYTE* p = &logo.px[((size_t)y * logo.w + x) * 4];
                        if (p[2] > 150 && letter(p)) {
                            s[0] += p[2], s[1] += p[1], s[2] += p[0], n++;
                        }
                    }
                }
                valid[t] = n >= 12;
                for (int c = 0; c < 3; c++) {
                    grad[t][c] = n > 0 ? (float)(s[c] / n) : 0.f;
                }
            }
            for (int t = 0; t < 64; t++) {
                if (valid[t]) {
                    continue;
                }
                for (int d = 1; d < 64; d++) {
                    const int a = t - d, b2 = t + d;
                    const int k = (b2 < 64 && valid[b2]) ? b2 : ((a >= 0 && valid[a]) ? a : -1);
                    if (k >= 0) {
                        grad[t][0] = grad[k][0], grad[t][1] = grad[k][1], grad[t][2] = grad[k][2];
                        break;
                    }
                }
            }
            float sm[64][3];
            for (int t = 0; t < 64; t++) {
                float s[3] = {}, n = 0;
                for (int d = -5; d <= 5; d++) {
                    const int k = std::max(0, std::min(63, t + d));
                    for (int c = 0; c < 3; c++) {
                        s[c] += grad[k][c];
                    }
                    n++;
                }
                for (int c = 0; c < 3; c++) {
                    sm[t][c] = s[c] / n;
                }
            }
            memcpy(grad, sm, sizeof(sm));
        }

        const int lh = (int)(H * 0.92);
        const float sc = (float)lh / logo.h;
        const int lw = (int)(logo.w * sc);

        int exW = 0, exH = 0;
        int capLogo = hdB - hdT + 1;
        const int exTopLogo = redB + std::max(3, (redB - redT) / 22);
        if (found) {
            capLogo = std::max(12, std::min(capLogo, (int)((logo.h - 3 - exTopLogo) / 1.12f)));
            PaintEX(capLogo, nullptr, -10000, -10000, [](int, int, float, float, float, float) {}, &exW, &exH);
        }
        const int exLogoX = found ? std::max(2, std::min(hdL, redR - exW)) : 0;
        if (found && exW > 0 && !exDir.empty()) { // (no exDir: the installer only borrows the art)
            // the game's main menu gets the same edit at the logo's own size (opaque on
            // black: the menu draws it additively, like the original)
            Bgra menu = logo;
            for (size_t i = 3; i < menu.px.size(); i += 4) {
                menu.px[i] = 255;
            }
            PaintEX(capLogo, grad, exLogoX, exTopLogo, [&](int x, int y, float r, float g, float bl, float a) {
                if (x < 0 || y < 0 || x >= menu.w || y >= menu.h || a <= 0) {
                    return;
                }
                BYTE* d = &menu.px[((size_t)y * menu.w + x) * 4];
                d[2] = (BYTE)std::min(255.f, d[2] * (1 - a) + r * a);
                d[1] = (BYTE)std::min(255.f, d[1] * (1 - a) + g * a);
                d[0] = (BYTE)std::min(255.f, d[0] * (1 - a) + bl * a);
            });
            {
                std::vector<float> cv((size_t)menu.w * menu.h);
                for (size_t i = 0; i < cv.size(); i++) {
                    const BYTE* p = &menu.px[i * 4];
                    cv[i] = std::min(1.f, std::max(p[0], std::max(p[1], p[2])) / 160.f);
                }
                const std::vector<float> gl = BoxBlur(cv, menu.w, menu.h, std::max(2, menu.h / 22));
                for (size_t i = 0; i < cv.size(); i++) {
                    const float g = gl[i] * (1.f - cv[i]) * 0.42f;
                    BYTE* p = &menu.px[i * 4];
                    p[2] = (BYTE)std::min(255.f, p[2] + 255.f * g);
                    p[1] = (BYTE)std::min(255.f, p[1] + 66.f * g);
                    p[0] = (BYTE)std::min(255.f, p[0] + 16.f * g);
                }
            }
            Image m;
            m.w = menu.w;
            m.h = menu.h;
            m.px.resize(menu.px.size());
            for (size_t i = 0; i < menu.px.size(); i += 4) {
                m.px[i + 0] = menu.px[i + 2], m.px[i + 1] = menu.px[i + 1], m.px[i + 2] = menu.px[i + 0], m.px[i + 3] = 255;
            }
            const std::string dir = exDir + "Data/Textures";
            MakeDirs(dir);
            SavePNG(dir + "/LugaruEX-Logo.png", m);
        }

        const int capH = found ? (int)(capLogo * sc) : (int)(H * 0.3f);
        int gw = 0, gh = 0;
        PaintEX(capH, nullptr, -10000, -10000, [](int, int, float, float, float, float) {}, &gw, &gh);
        const int gap = std::max(2, capH / 7);

        const int contentL = (int)((found ? redL : 0) * sc);
        const int exLeft = found ? (int)(exLogoX * sc) : (int)(logo.w * 0.7f * sc) + gap;
        const int contentR = std::max(found ? (int)(redR * sc) : lw, exLeft + gw);
        const int exTop = found ? (int)(exTopLogo * sc) : (int)(logo.h * 0.6f * sc);
        const int contentT = found ? (int)(redT * sc) : 0, contentB = found ? exTop + gh : lh;
        const int lx = (W - (contentR - contentL)) / 2 - contentL, ly = (H - (contentB - contentT)) / 2 - contentT;

        // the logo, scaled, over the backdrop (premultiplied "over")
        for (int y = std::max(0, ly); y < std::min(H, ly + lh); y++) {
            for (int x = std::max(0, lx); x < std::min(W, lx + lw); x++) {
                float c[4];
                sampleArea(logo, (x - lx) / sc, (y - ly) / sc, (x - lx + 1) / sc, (y - ly + 1) / sc, c);
                BYTE* d = &bits[((size_t)y * W + x) * 4];
                const float ia = 1.f - c[3] / 255.f;
                for (int k = 0; k < 3; k++) {
                    d[k] = (BYTE)std::min(255.f, c[k] + d[k] * ia);
                }
            }
        }

        std::vector<float> cover((size_t)W * H, 0.f);
        for (int y = std::max(0, ly); y < std::min(H, ly + lh); y++) {
            for (int x = std::max(0, lx); x < std::min(W, lx + lw); x++) {
                const int sx = std::min(logo.w - 1, (int)((x - lx) / sc)), sy = std::min(logo.h - 1, (int)((y - ly) / sc));
                cover[(size_t)y * W + x] = logo.px[((size_t)sy * logo.w + sx) * 4 + 3] / 255.f;
            }
        }

        BYTE* D = bits;
        PaintEX(capH, found ? grad : nullptr, lx + exLeft, ly + exTop, [&](int x, int y, float r, float g, float bl, float a) {
            if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) {
                return;
            }
            BYTE* d = &D[((size_t)y * W + x) * 4];
            d[2] = (BYTE)std::min(255.f, d[2] * (1 - a) + r * a);
            d[1] = (BYTE)std::min(255.f, d[1] * (1 - a) + g * a);
            d[0] = (BYTE)std::min(255.f, d[0] * (1 - a) + bl * a);
            if (r + g + bl > 0) {
                float& cv = cover[(size_t)y * W + x];
                cv = std::max(cv, a);
            }
        });
        const std::vector<float> glow = BoxBlur(cover, W, H, std::max(2, H / 16));
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                const float gl = glow[(size_t)y * W + x] * (1.f - cover[(size_t)y * W + x]) * 0.55f;
                BYTE* d = &D[((size_t)y * W + x) * 4];
                d[2] = (BYTE)std::min(255.f, d[2] + 255.f * gl);
                d[1] = (BYTE)std::min(255.f, d[1] + 70.f * gl);
                d[0] = (BYTE)std::min(255.f, d[0] + 18.f * gl);
            }
        }
    }
    // a gentle vignette draws the eye to the middle
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const float u = (x + .5f) / W - .5f, v = ((y + .5f) / H - .5f) * 0.6f;
            const float k = 1.f - 0.55f * std::min(1.f, (u * u + v * v) * 2.6f);
            BYTE* d = &bits[((size_t)y * W + x) * 4];
            d[0] = (BYTE)(d[0] * k), d[1] = (BYTE)(d[1] * k), d[2] = (BYTE)(d[2] * k);
        }
    }
    result.w = W;
    result.h = H;
    result.px.resize(dst.px.size());
    for (size_t i = 0; i < dst.px.size(); i += 4) {
        result.px[i + 0] = dst.px[i + 2], result.px[i + 1] = dst.px[i + 1], result.px[i + 2] = dst.px[i + 0], result.px[i + 3] = 255;
    }
}
