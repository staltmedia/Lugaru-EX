// LugaruEX launcher - Xbox 360 style controller visualizer (GPLv2+)
// Clean anti-aliased outlines (GDI+). Buttons sink and fill when pressed,
// sticks tilt/rotate with the stick and sink when clicked, triggers fill.
#include "padviz.h"

#include <algorithm>
#include <cmath>
namespace Gdiplus
{
using std::max;
using std::min;
}
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace
{
// the design is laid out in a 1000 x 720 box
const float kW = 1000.f, kH = 720.f;

struct Ctx
{
    Graphics* g;
    float ox, oy, k; // design units -> pixels
    PointF P(float x, float y) const { return PointF(ox + x * k, oy + y * k); }
    float S(float d) const { return d * k; }
};

Color C(COLORREF c, int a = 255)
{
    return Color((BYTE)a, GetRValue(c), GetGValue(c), GetBValue(c));
}

COLORREF mix(COLORREF a, COLORREF b, float t)
{
    return RGB((int)(GetRValue(a) * (1 - t) + GetRValue(b) * t), (int)(GetGValue(a) * (1 - t) + GetGValue(b) * t),
               (int)(GetBValue(a) * (1 - t) + GetBValue(b) * t));
}

void bez(GraphicsPath& p, const Ctx& c, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3)
{
    p.AddBezier(c.P(x0, y0), c.P(x1, y1), c.P(x2, y2), c.P(x3, y3));
}

// right half of the body outline, top centre -> bottom centre; mirrored for the left
const float kBody[6][8] = {
    { 500, 150, 620, 150, 760, 155, 830, 180 }, // top edge
    { 830, 180, 915, 205, 962, 290, 978, 410 }, // shoulder
    { 978, 410, 998, 540, 975, 650, 905, 690 }, // grip, outside
    { 905, 690, 845, 722, 775, 692, 745, 645 }, // grip tip
    { 745, 645, 715, 610, 670, 598, 610, 597 }, // grip, inside
    { 610, 597, 570, 596, 530, 596, 500, 596 }, // bottom centre
};

void bodyPath(GraphicsPath& p, const Ctx& c)
{
    for (auto& s : kBody) {
        bez(p, c, s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
    }
    for (int i = 5; i >= 0; i--) {
        const float* s = kBody[i];
        bez(p, c, kW - s[6], s[7], kW - s[4], s[5], kW - s[2], s[3], kW - s[0], s[1]);
    }
    p.CloseFigure();
}

void circleAt(const Ctx& c, const Pen* pen, const Brush* br, float x, float y, float r)
{
    const PointF q = c.P(x - r, y - r);
    const float d = c.S(2 * r);
    if (br) {
        c.g->FillEllipse(br, q.X, q.Y, d, d);
    }
    if (pen) {
        c.g->DrawEllipse(pen, q.X, q.Y, d, d);
    }
}

void roundRect(GraphicsPath& p, const Ctx& c, float l, float t, float r, float b, float rad)
{
    const float d = rad * 2;
    const PointF a = c.P(l, t);
    const float w = c.S(r - l), h = c.S(b - t), dd = c.S(d);
    p.AddArc(a.X, a.Y, dd, dd, 180, 90);
    p.AddArc(a.X + w - dd, a.Y, dd, dd, 270, 90);
    p.AddArc(a.X + w - dd, a.Y + h - dd, dd, dd, 0, 90);
    p.AddArc(a.X, a.Y + h - dd, dd, dd, 90, 90);
    p.CloseFigure();
}

void text(const Ctx& c, float x, float y, float size, const wchar_t* s, Color col)
{
    FontFamily ff(L"Segoe UI");
    Font f(&ff, std::max(6.f, c.S(size)), FontStyleBold, UnitPixel);
    SolidBrush b(col);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    c.g->DrawString(s, -1, &f, c.P(x, y + size * 0.04f), &sf, &b);
}

ULONG_PTR g_token = 0;
} // namespace

void PadViz_Draw(HDC dc, const RECT& r, const PadState* s, COLORREF accent, bool showLabels)
{
    if (!g_token) {
        GdiplusStartupInput in;
        GdiplusStartup(&g_token, &in, NULL);
    }
    const float rw = (float)(r.right - r.left), rh = (float)(r.bottom - r.top);
    const float k = std::min(rw / kW, rh / kH);
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    Ctx c{ &g, r.left + (rw - kW * k) / 2, r.top + (rh - kH * k) / 2, k };

    const bool on = s && s->connected;
    const COLORREF ink = on ? RGB(236, 238, 242) : RGB(125, 125, 128); // outline colour
    const float lw = std::max(1.2f, c.S(7.f));
    Pen line(C(ink, on ? 235 : 200), lw);
    line.SetLineJoin(LineJoinRound);
    line.SetStartCap(LineCapRound);
    line.SetEndCap(LineCapRound);
    Pen thin(C(ink, on ? 170 : 140), std::max(1.f, lw * 0.6f));
    thin.SetLineJoin(LineJoinRound);
    SolidBrush glow(C(accent, 205));
    SolidBrush soft(C(accent, 70));
    Pen hot(C(mix(accent, RGB(255, 255, 255), 0.35f)), lw);
    hot.SetLineJoin(LineJoinRound);

    auto btn = [&](int i) { return s && i >= 0 && i < PAD_BUTTONS && s->btn[i]; };

    // --- triggers (behind, above the bumpers): fill from the bottom with pressure
    auto trigger = [&](bool right, float v) {
        auto X = [&](float x) { return right ? kW - x : x; };
        GraphicsPath p;
        bez(p, c, X(215), 125, X(205), 70, X(225), 30, X(270), 26);
        bez(p, c, X(270), 26, X(320), 22, X(345), 60, X(348), 125);
        p.CloseFigure();
        if (v > 0.02f) {
            Region clip(&p);
            g.SetClip(&clip);
            const PointF a = c.P(0, 125 - 105 * std::min(1.f, v));
            g.FillRectangle(&glow, c.P(0, 0).X, a.Y, c.S(kW), c.P(0, 130).Y - a.Y);
            g.ResetClip();
        }
        g.DrawPath(v > 0.5f ? &hot : &line, &p);
    };
    trigger(false, s ? s->lt : 0);
    trigger(true, s ? s->rt : 0);

    // --- bumpers: a band hugging each shoulder
    auto bumper = [&](bool right, bool down) {
        auto X = [&](float x) { return right ? kW - x : x; };
        GraphicsPath p;
        bez(p, c, X(110), 262, X(122), 200, X(200), 148, X(380), 140);
        bez(p, c, X(380), 140, X(392), 140, X(392), 168, X(380), 170);
        bez(p, c, X(380), 170, X(225), 178, X(160), 215, X(146), 268);
        bez(p, c, X(146), 268, X(140), 285, X(108), 280, X(110), 262);
        p.CloseFigure();
        if (down) {
            g.FillPath(&glow, &p);
        }
        g.DrawPath(down ? &hot : &line, &p);
    };
    bumper(false, btn(9));
    bumper(true, btn(10));

    // --- body
    {
        GraphicsPath p;
        bodyPath(p, c);
        g.DrawPath(&line, &p);
    }

    // --- sticks: a well, and a cap that tilts toward the push (squashes along
    // that direction and turns), with the four grip dots; clicking sinks it
    auto stick = [&](float x, float y, float sx, float sy, bool click) {
        circleAt(c, &thin, nullptr, x, y, 80);
        const float mag = std::min(1.f, sqrtf(sx * sx + sy * sy));
        const float ang = (mag > 0.05f) ? atan2f(sy, sx) * 57.2958f : 0.f;
        const float capR = click ? 46.f : 54.f;
        const PointF cc = c.P(x + sx * 24, y + sy * 24);
        GraphicsState st = g.Save();
        g.TranslateTransform(cc.X, cc.Y);
        g.RotateTransform(ang);
        g.ScaleTransform(1.f - 0.22f * mag, 1.f);
        Ctx l{ &g, 0, 0, k };
        if (click) {
            circleAt(l, nullptr, &glow, 0, 0, capR);
        }
        circleAt(l, click ? &hot : &line, nullptr, 0, 0, capR);
        circleAt(l, &thin, nullptr, 0, 0, capR * 0.68f);
        SolidBrush dot(C(click ? RGB(255, 255, 255) : ink, on ? 230 : 160));
        for (int d = 0; d < 4; d++) {
            const float a = d * 1.5708f + 0.7854f;
            circleAt(l, nullptr, &dot, cosf(a) * capR * 0.84f, sinf(a) * capR * 0.84f, 5.5f);
        }
        g.Restore(st);
    };
    stick(265, 330, s ? s->lx : 0, s ? s->ly : 0, btn(7));
    stick(632, 498, s ? s->rx : 0, s ? s->ry : 0, btn(8));

    // --- D-pad: round base with a cross; a pressed arm fills
    {
        const float x = 368, y = 498, L = 60, a = 21;
        circleAt(c, &thin, nullptr, x, y, 80);
        GraphicsPath p;
        PointF pts[] = { c.P(x - a, y - L), c.P(x + a, y - L), c.P(x + a, y - a), c.P(x + L, y - a), c.P(x + L, y + a), c.P(x + a, y + a),
                         c.P(x + a, y + L), c.P(x - a, y + L), c.P(x - a, y + a), c.P(x - L, y + a), c.P(x - L, y - a), c.P(x - a, y - a) };
        p.AddPolygon(pts, 12);
        auto arm = [&](bool down, float l, float t, float rr, float b) {
            if (down) {
                g.FillRectangle(&glow, c.P(l, t).X, c.P(l, t).Y, c.S(rr - l), c.S(b - t));
            }
        };
        arm(btn(11), x - a, y - L, x + a, y - a);
        arm(btn(12), x - a, y + a, x + a, y + L);
        arm(btn(13), x - L, y - a, x - a, y + a);
        arm(btn(14), x + a, y - a, x + L, y + a);
        g.DrawPath(&line, &p);
    }

    // --- face buttons in Xbox colours: outline + letter; pressed = sinks and fills
    auto face = [&](float x, float y, bool down, COLORREF col, const wchar_t* t) {
        const float rr = down ? 30.f : 34.f;
        Pen ring(C(col, on ? 255 : 150), lw);
        if (down) {
            SolidBrush f(C(col, 235));
            circleAt(c, &ring, &f, x, y + 3, rr);
        } else {
            circleAt(c, &ring, nullptr, x, y, rr);
        }
        if (showLabels && c.S(rr) >= 6) {
            text(c, x, down ? y + 3 : y, 34, t, down ? Color(255, 255, 255, 255) : C(col, on ? 255 : 150));
        }
    };
    face(745, 405, btn(0), RGB(96, 200, 80), L"A");
    face(815, 330, btn(1), RGB(232, 72, 64), L"B");
    face(675, 330, btn(2), RGB(70, 140, 240), L"X");
    face(745, 255, btn(3), RGB(245, 200, 50), L"Y");

    // --- Back / Start (small ovals) and the guide button (blank)
    auto pill = [&](float x, float y, bool down) {
        GraphicsPath p;
        roundRect(p, c, x - 22, y - 13 + (down ? 2 : 0), x + 22, y + 13 + (down ? 2 : 0), 13);
        if (down) {
            g.FillPath(&glow, &p);
        }
        g.DrawPath(down ? &hot : &thin, &p);
    };
    pill(410, 330, btn(4));
    pill(590, 330, btn(6));
    if (btn(5)) {
        circleAt(c, &hot, &soft, 500, 302, 46);
    } else {
        circleAt(c, &line, nullptr, 500, 302, 48);
    }
    circleAt(c, &thin, nullptr, 500, 302, 60);

    if (!on) {
        text(c, 500, 660, 40, s ? L"disconnected - waiting" : L"no controller", Color(220, 200, 200, 200));
    }
}
