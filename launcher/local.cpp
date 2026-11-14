// LugaruEX launcher - Local tab: split-screen players and screen layout (GPLv2+)
#include "local.h"

#include "padviz.h"
#include "resource.h"
#include "skins.h"
#include "customskins.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
struct Player
{
    std::string id;
    std::wstring name;
    std::string ch = "random"; // default | random | <model>:<skin path> (new players: random)
    int seat = 0;                    // which part of the screen (0 = top-left / first)
    int rank = 0;                    // saved player number (0 = player 1): order when several join at once
    float x = 0, y = 0, w = 1, h = 1; // screen region (0..1), from the layout
};

const int kMax = 4;
const COLORREF kColor[kMax] = { RGB(70, 140, 245), RGB(230, 70, 55), RGB(80, 200, 80), RGB(240, 190, 40) };

HWND g_page = NULL, g_canvas = NULL;
std::vector<Player> g_pl;
bool g_sideBySide = false; // two players: side by side instead of top / bottom
std::vector<std::pair<std::string, std::wstring>> g_chars;

// drag state: drop a screen onto another to swap the two players' places
int g_drag = -1, g_hover = -1;
POINT g_dragPt = {};

std::string narrow(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w) {
        s += (c > 0 && c < 128) ? (char)c : '_';
    }
    return s;
}
std::wstring widen(const std::string& s)
{
    return std::wstring(s.begin(), s.end());
}

COLORREF mix(COLORREF a, COLORREF b, float t)
{
    return RGB((int)(GetRValue(a) * (1 - t) + GetRValue(b) * t), (int)(GetGValue(a) * (1 - t) + GetGValue(b) * t),
               (int)(GetBValue(a) * (1 - t) + GetBValue(b) * t));
}

// fixed layouts: 1 full, 2 top/bottom or side by side, 3 = two on top + one wide, 4 corners
void seatRect(int seat, int n, float& x, float& y, float& w, float& h)
{
    x = 0, y = 0, w = 1, h = 1;
    if (n == 2) {
        if (g_sideBySide) {
            x = seat * 0.5f, w = 0.5f;
        } else {
            y = seat * 0.5f, h = 0.5f;
        }
    } else if (n == 3) {
        if (seat < 2) {
            x = seat * 0.5f, w = 0.5f, h = 0.5f;
        } else {
            y = 0.5f, h = 0.5f;
        }
    } else if (n >= 4) {
        x = (seat % 2) * 0.5f, y = (seat / 2) * 0.5f, w = 0.5f, h = 0.5f;
    }
}

void layout()
{
    const int n = (int)g_pl.size();
    // keep seats a permutation of 0..n-1 (players may have left or joined)
    std::vector<int> order(n);
    for (int i = 0; i < n; i++) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [](int a, int b) { return g_pl[a].seat < g_pl[b].seat; });
    for (int k = 0; k < n; k++) {
        g_pl[order[k]].seat = k;
    }
    for (auto& p : g_pl) {
        seatRect(p.seat, n, p.x, p.y, p.w, p.h);
    }
    if (g_canvas) {
        InvalidateRect(g_canvas, NULL, FALSE);
    }
}

const char* const kKeyboard = "keyboard"; // player 1 on keyboard and mouse only (their controller is another player)

// the connected controllers, with numbers when two have the same name
std::vector<std::pair<std::string, std::wstring>> connectedPads()
{
    std::vector<std::pair<std::string, std::wstring>> out;
    for (auto& s : Pad_List()) {
        if (s.connected) {
            out.push_back({ s.id, s.name });
        }
    }
    for (size_t i = 0; i < out.size(); i++) {
        int same = 0, mine = 0;
        for (size_t k = 0; k < out.size(); k++) {
            if (out[k].second == out[i].second) {
                same++;
                if (k <= i) {
                    mine++;
                }
            }
        }
        if (same > 1) {
            out[i].second += L" " + std::to_wstring(mine);
        }
    }
    return out;
}

// what each row's input list holds (ids, in list order)
std::vector<std::string> g_devIds[kMax];

void fillDevices(int i)
{
    HWND c = GetDlgItem(g_page, IDC_LC_DEV1 + i);
    std::vector<std::string> ids;
    std::vector<std::wstring> labels;
    const auto pads = connectedPads();
    if (i == 0) {
        ids.push_back(kKeyboard);
        labels.push_back(L"Keyboard and mouse");
        for (auto& p : pads) {
            ids.push_back(p.first);
            labels.push_back(L"Keyboard + " + p.second);
        }
    } else {
        for (auto& p : pads) {
            ids.push_back(p.first);
            labels.push_back(p.second);
        }
    }
    if (ids != g_devIds[i]) {
        g_devIds[i] = ids;
        SendMessageW(c, CB_RESETCONTENT, 0, 0);
        for (auto& l : labels) {
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)l.c_str());
        }
    }
    for (size_t k = 0; k < ids.size(); k++) {
        if (ids[k] == g_pl[i].id) {
            SendMessageW(c, CB_SETCURSEL, k, 0);
        }
    }
}

void refreshRows()
{
    for (int i = 0; i < kMax; i++) {
        const bool show = i < (int)g_pl.size();
        const int ids[] = { IDC_LC_LABEL1 + i, IDC_LC_DEV1 + i, IDC_LC_CHAR1 + i };
        for (int id : ids) {
            ShowWindow(GetDlgItem(g_page, id), show ? SW_SHOW : SW_HIDE);
        }
        ShowWindow(GetDlgItem(g_page, IDC_LC_REMOVE1 + i), SW_HIDE); // (a player leaves with their controller)
        if (!show) {
            continue;
        }
        const Player& p = g_pl[i];
        SetDlgItemTextW(g_page, IDC_LC_LABEL1 + i, (L"P" + std::to_wstring(i + 1)).c_str());
        fillDevices(i);
        HWND c = GetDlgItem(g_page, IDC_LC_CHAR1 + i);
        // player 1's character is the Character tab's (model, skin and clothes)
        const bool p1 = (i == 0);
        const int want = p1 ? 1 : (int)g_chars.size();
        if ((int)SendMessageW(c, CB_GETCOUNT, 0, 0) != want || (p1 != !IsWindowEnabled(c))) {
            SendMessageW(c, CB_RESETCONTENT, 0, 0);
            if (p1) {
                SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"As on the Character tab");
            } else {
                for (auto& ch : g_chars) {
                    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)ch.second.c_str());
                }
            }
            EnableWindow(c, !p1);
        }
        SendMessageW(c, CB_SETCURSEL, 0, 0);
        if (p1) {
            continue;
        }
        for (size_t k = 0; k < g_chars.size(); k++) {
            if (g_chars[k].first == p.ch) {
                SendMessageW(c, CB_SETCURSEL, k, 0);
            }
        }
    }
    ShowWindow(GetDlgItem(g_page, IDC_LC_EMPTY), g_pl.empty() ? SW_SHOW : SW_HIDE);
}

// controllers seen before (saved, or unplugged this session): their character and seat
// come back with them
std::vector<Player> g_known;

void remember(const Player& p)
{
    auto it = std::find_if(g_known.begin(), g_known.end(), [&](const Player& k) { return k.id == p.id; });
    if (it != g_known.end()) {
        *it = p;
    } else {
        g_known.push_back(p);
    }
}

// the players are the controllers connected right now (none = player 1 on the keyboard):
// a controller switched on joins, one switched off leaves
void sync()
{
    bool changed = false;
    for (size_t i = 0; i < g_pl.size();) {
        PadState* s = Pad_Find(g_pl[i].id);
        if ((s && s->connected) || g_pl[i].id == kKeyboard) {
            i++;
            continue;
        }
        remember(g_pl[i]);
        g_pl.erase(g_pl.begin() + i);
        changed = true;
    }
    std::vector<Player> joining;
    for (auto& s : Pad_List()) {
        if (!s.connected) {
            continue;
        }
        auto it = std::find_if(g_pl.begin(), g_pl.end(), [&](const Player& p) { return p.id == s.id; });
        if (it != g_pl.end()) {
            if (it->name != s.name) {
                it->name = s.name;
                changed = true;
            }
            continue;
        }
        auto k = std::find_if(g_known.begin(), g_known.end(), [&](const Player& p) { return p.id == s.id; });
        Player p;
        if (k != g_known.end()) {
            p = *k;
        } else {
            p.seat = 1000 + (int)joining.size(); // after everyone already here
            p.rank = 1000 + (int)joining.size();
        }
        p.id = s.id;
        p.name = s.name;
        joining.push_back(p);
    }
    // several at once (the launcher opening): in their saved order, so player 1 stays player 1
    std::stable_sort(joining.begin(), joining.end(), [](const Player& a, const Player& b) { return a.rank < b.rank; });
    const bool late = !g_pl.empty();
    for (Player& p : joining) {
        if ((int)g_pl.size() >= kMax) {
            break;
        }
        if (late) {
            p.seat = 1000 + (int)g_pl.size(); // joining later: the next free part of the screen
        }
        g_pl.push_back(p);
        changed = true;
    }
    if (changed) {
        layout();
        refreshRows();
    }
}

// ---- canvas
RECT screenRect(HWND w)
{
    RECT rc;
    GetClientRect(w, &rc);
    const int m = 4;
    float cw = (float)(rc.right - 2 * m), ch = (float)(rc.bottom - 2 * m);
    float sw = cw, sh = cw * 9 / 16;
    if (sh > ch) {
        sh = ch;
        sw = ch * 16 / 9;
    }
    RECT s;
    s.left = (LONG)(m + (cw - sw) / 2);
    s.top = (LONG)(m + (ch - sh) / 2);
    s.right = s.left + (LONG)sw;
    s.bottom = s.top + (LONG)sh;
    return s;
}

RECT regionPx(const RECT& s, const Player& p)
{
    const float W = (float)(s.right - s.left), H = (float)(s.bottom - s.top);
    RECT r = { (LONG)lroundf(s.left + p.x * W), (LONG)lroundf(s.top + p.y * H), (LONG)lroundf(s.left + (p.x + p.w) * W),
               (LONG)lroundf(s.top + (p.y + p.h) * H) };
    return r;
}

int hitTest(HWND w, int mx, int my)
{
    const RECT s = screenRect(w);
    for (int i = 0; i < (int)g_pl.size(); i++) {
        RECT rr = regionPx(s, g_pl[i]);
        if (mx >= rr.left && mx < rr.right && my >= rr.top && my < rr.bottom) {
            return i;
        }
    }
    return -1;
}

void paint(HWND w, HDC dc)
{
    RECT rc;
    GetClientRect(w, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(30, 28, 28));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    const RECT s = screenRect(w);
    HBRUSH scr = CreateSolidBrush(RGB(12, 12, 14));
    FillRect(dc, &s, scr);
    DeleteObject(scr);

    const DWORD now = GetTickCount();
    for (int i = 0; i < (int)g_pl.size(); i++) {
        const Player& p = g_pl[i];
        RECT r = regionPx(s, p);
        PadState* st = Pad_Find(p.id);
        const bool keyboard = p.id == kKeyboard;
        const bool conn = keyboard || (st && st->connected);
        const bool flash = st && st->connected && now - st->lastActive < 350; // this controller is being used
        const bool target = g_drag >= 0 && i == g_hover && i != g_drag;
        HBRUSH fill = CreateSolidBrush(mix(RGB(20, 20, 22), kColor[i], (flash || target) ? 0.45f : (i == g_drag ? 0.1f : 0.22f)));
        FillRect(dc, &r, fill);
        DeleteObject(fill);
        HPEN pen = CreatePen(target ? PS_DOT : PS_SOLID, (flash && !target) ? 4 : (target ? 1 : 2), target ? RGB(255, 255, 255) : (conn ? kColor[i] : RGB(110, 110, 110)));
        HGDIOBJ o1 = SelectObject(dc, pen), o2 = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left + 1, r.top + 1, r.right, r.bottom);
        SelectObject(dc, o1);
        SelectObject(dc, o2);
        DeleteObject(pen);
        // player tag
        HFONT f = CreateFontW(-std::max(11, (int)((r.bottom - r.top) * 0.14f)), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HGDIOBJ of = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, conn ? kColor[i] : RGB(150, 150, 150));
        RECT tr = { r.left + 6, r.top + 3, r.right - 4, r.bottom };
        DrawTextW(dc, (L"P" + std::to_wstring(i + 1)).c_str(), -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(dc, of);
        DeleteObject(f);
        // live controller picture for this player's controller only
        const int pw = r.right - r.left, ph = r.bottom - r.top;
        RECT vr = { r.left + pw / 6, r.top + ph / 6, r.right - pw / 6, r.bottom - ph / 8 };
        if (keyboard) {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, mix(kColor[i], RGB(255, 255, 255), 0.4f));
            DrawTextW(dc, L"Keyboard", -1, &vr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            PadViz_Draw(dc, vr, st, kColor[i], (vr.right - vr.left) > 110);
        }
    }
    if (g_pl.empty()) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(170, 170, 170));
        RECT tr = s;
        DrawTextW(dc, L"P1 - keyboard and mouse", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

LRESULT CALLBACK CanvasProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(w, &ps);
            RECT rc;
            GetClientRect(w, &rc);
            HDC m = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ ob = SelectObject(m, bmp);
            paint(w, m);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, m, 0, 0, SRCCOPY);
            SelectObject(m, ob);
            DeleteObject(bmp);
            DeleteDC(m);
            EndPaint(w, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(w, &pt);
            SetCursor(LoadCursor(NULL, (g_pl.size() > 1 && hitTest(w, pt.x, pt.y) >= 0) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        case WM_LBUTTONDOWN: {
            if (g_pl.size() > 1) {
                g_drag = hitTest(w, (short)LOWORD(lp), (short)HIWORD(lp));
                g_hover = g_drag;
                if (g_drag >= 0) {
                    SetCapture(w);
                }
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_drag >= 0 && (wp & MK_LBUTTON) && g_pl.size() == 2) {
                // two players: the screen goes where it is dragged - toward a side gives
                // side by side, toward the top or bottom gives top / bottom (live preview)
                const RECT s = screenRect(w);
                const float nx = ((short)LOWORD(lp) - s.left) / (float)std::max(1L, s.right - s.left) - 0.5f;
                const float ny = ((short)HIWORD(lp) - s.top) / (float)std::max(1L, s.bottom - s.top) - 0.5f;
                if (fabsf(nx) > 0.08f || fabsf(ny) > 0.08f) {
                    const bool side = fabsf(nx) * 16.f > fabsf(ny) * 9.f;
                    const int seat = side ? (nx < 0 ? 0 : 1) : (ny < 0 ? 0 : 1);
                    if (side != g_sideBySide || g_pl[g_drag].seat != seat) {
                        g_sideBySide = side;
                        g_pl[g_drag].seat = seat;
                        g_pl[1 - g_drag].seat = 1 - seat;
                        layout();
                    }
                }
                return 0;
            }
            if (g_drag >= 0 && (wp & MK_LBUTTON)) {
                const int h = hitTest(w, (short)LOWORD(lp), (short)HIWORD(lp));
                if (h != g_hover) {
                    g_hover = h;
                    InvalidateRect(w, NULL, FALSE);
                }
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (g_drag >= 0) {
                const int h = hitTest(w, (short)LOWORD(lp), (short)HIWORD(lp));
                if (g_pl.size() != 2 && h >= 0 && h != g_drag) {
                    std::swap(g_pl[g_drag].seat, g_pl[h].seat); // the two players trade places
                    layout();
                }
                g_drag = g_hover = -1;
                ReleaseCapture();
                InvalidateRect(w, NULL, FALSE);
            }
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
} // namespace

void Local_Register(HINSTANCE inst)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = CanvasProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"LugaruEXLayout";
    RegisterClassW(&wc);
}

// one list for every player: Default, Random, the built-in skins, player-made
// skins, and "New rabbit/wolf skin..." which imports an image
static void fillChars()
{
    g_chars.clear();
    g_chars.push_back({ "default", L"Default" });
    g_chars.push_back({ "random", L"Random" });
    for (const SkinInfo& s : kSkins) {
        g_chars.push_back({ std::string(s.model) + ":" + s.path, std::wstring(strcmp(s.model, "wolf") == 0 ? L"Wolf - " : L"Rabbit - ") + s.label });
    }
    for (const CustomSkin& s : CustomSkins_List()) {
        g_chars.push_back({ s.model + ":" + s.enginePath, std::wstring(s.model == "wolf" ? L"Wolf - " : L"Rabbit - ") + s.label });
    }
    g_chars.push_back({ "new:rabbit", L"New rabbit skin..." });
    g_chars.push_back({ "new:wolf", L"New wolf skin..." });
    for (int i = 0; i < kMax; i++) {
        HWND c = GetDlgItem(g_page, IDC_LC_CHAR1 + i);
        SendMessageW(c, CB_RESETCONTENT, 0, 0);
        for (auto& ch : g_chars) {
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)ch.second.c_str());
        }
        SendMessageW(c, CB_SETCURSEL, 0, 0);
    }
}

void Local_Init(HWND page, const std::wstring& iniPath)
{
    g_page = page;
    g_canvas = GetDlgItem(page, IDC_LC_CANVAS);
    fillChars();
    // saved players (including ones whose controller is unplugged right now)
    const std::string f = narrow(iniPath);
    const int n = std::min(kMax, (int)GetPrivateProfileIntA("Local", "Count", 0, f.c_str()));
    char buf[256];
    GetPrivateProfileStringA("Local", "Split2", "topbottom", buf, sizeof(buf), f.c_str());
    g_sideBySide = std::string(buf) == "sidebyside";
    for (int i = 0; i < n; i++) {
        char key[32];
        Player p;
        snprintf(key, sizeof(key), "P%d", i + 1);
        GetPrivateProfileStringA("Local", key, "", buf, sizeof(buf), f.c_str());
        p.id = buf;
        if (p.id.empty()) {
            continue;
        }
        snprintf(key, sizeof(key), "P%dName", i + 1);
        GetPrivateProfileStringA("Local", key, "Controller", buf, sizeof(buf), f.c_str());
        p.name = widen(buf);
        snprintf(key, sizeof(key), "P%dChar", i + 1);
        GetPrivateProfileStringA("Local", key, "random", buf, sizeof(buf), f.c_str());
        p.ch = buf;
        snprintf(key, sizeof(key), "P%dSeat", i + 1);
        p.seat = GetPrivateProfileIntA("Local", key, i, f.c_str());
        p.rank = i;
        if (i == 0 && p.id == kKeyboard) {
            p.name = L"Keyboard and mouse";
            g_pl.push_back(p); // player 1 chose keyboard and mouse only: always there
            continue;
        }
        g_known.push_back(p); // a player again once their controller is on
    }
    // controllers that were off when the settings were saved: id|character|name
    for (int i = 1; i <= 16; i++) {
        char key[32];
        snprintf(key, sizeof(key), "Known%d", i);
        GetPrivateProfileStringA("Local", key, "", buf, sizeof(buf), f.c_str());
        const std::string v = buf;
        const size_t a = v.find('|'), b = a == std::string::npos ? a : v.find('|', a + 1);
        if (b == std::string::npos) {
            continue;
        }
        Player p;
        p.id = v.substr(0, a);
        p.ch = v.substr(a + 1, b - a - 1);
        p.name = widen(v.substr(b + 1));
        p.rank = p.seat = 100 + i;
        if (!p.id.empty() && std::none_of(g_known.begin(), g_known.end(), [&](const Player& k) { return k.id == p.id; })) {
            g_known.push_back(p);
        }
    }
    SetDlgItemTextW(page, IDC_LC_NOTE,
                    L"Click and drag a player's screen to move it.");
    layout();
    refreshRows();
}

INT_PTR Local_Proc(HWND page, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_CTLCOLORSTATIC: {
            const int id = GetDlgCtrlID((HWND)lp);
            if (id >= IDC_LC_LABEL1 && id < IDC_LC_LABEL1 + kMax) {
                HDC dc = (HDC)wp;
                const int i = id - IDC_LC_LABEL1;
                PadState* s = (i < (int)g_pl.size()) ? Pad_Find(g_pl[i].id) : nullptr;
                const bool on = (s && s->connected) || (i < (int)g_pl.size() && g_pl[i].id == kKeyboard);
                SetTextColor(dc, on ? mix(kColor[i], RGB(0, 0, 0), 0.25f) : RGB(120, 120, 120));
                SetBkMode(dc, TRANSPARENT);
                return (INT_PTR)GetStockObject(NULL_BRUSH);
            }
            break;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id >= IDC_LC_DEV1 && id < IDC_LC_DEV1 + kMax && code == CBN_SELCHANGE) {
                // a player takes another input: whoever had that controller gets this one
                const int i = id - IDC_LC_DEV1;
                const int s = (int)SendDlgItemMessageW(page, id, CB_GETCURSEL, 0, 0);
                if (i < (int)g_pl.size() && s >= 0 && s < (int)g_devIds[i].size()) {
                    const std::string want = g_devIds[i][s];
                    auto other = std::find_if(g_pl.begin(), g_pl.end(), [&](const Player& p) { return p.id == want; });
                    if (want == g_pl[i].id) {
                        // (no change)
                    } else if (other != g_pl.end()) {
                        const int j = (int)(other - g_pl.begin());
                        if (g_pl[i].id == kKeyboard) {
                            // player 1 takes that controller back: its player leaves
                            g_pl[0].id = want;
                            g_pl[0].name = other->name;
                            g_pl.erase(g_pl.begin() + j);
                        } else {
                            std::swap(g_pl[i].id, g_pl[j].id);
                            std::swap(g_pl[i].name, g_pl[j].name);
                        }
                    } else {
                        // keyboard only for player 1: the controller it had becomes the next player (sync)
                        g_pl[i].id = want;
                        g_pl[i].name = want == kKeyboard ? L"Keyboard and mouse" : g_pl[i].name;
                    }
                    sync();
                    layout();
                    refreshRows();
                }
                return TRUE;
            }
            if (id >= IDC_LC_CHAR1 && id < IDC_LC_CHAR1 + kMax && code == CBN_SELCHANGE) {
                const int i = id - IDC_LC_CHAR1;
                const int s = (int)SendDlgItemMessageW(page, id, CB_GETCURSEL, 0, 0);
                if (i < (int)g_pl.size() && s >= 0 && s < (int)g_chars.size()) {
                    const std::string key = g_chars[s].first;
                    if (key.rfind("new:", 0) == 0) {
                        CustomSkin cs;
                        if (CustomSkins_Import(GetAncestor(page, GA_ROOT), key.substr(4), cs)) {
                            g_pl[i].ch = cs.model + ":" + cs.enginePath;
                        }
                        fillChars(); // the new skin is now in every player's list
                    } else {
                        g_pl[i].ch = key;
                    }
                }
                refreshRows();
                return TRUE;
            }
            if (id >= IDC_LC_REMOVE1 && id < IDC_LC_REMOVE1 + kMax) {
                const int i = id - IDC_LC_REMOVE1;
                if (i < (int)g_pl.size()) {
                    g_pl.erase(g_pl.begin() + i);
                    layout();
                    refreshRows();
                }
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

void Local_Tick()
{
    if (!g_page) {
        return;
    }
    sync();
    if (IsWindowVisible(g_page)) {
        InvalidateRect(g_canvas, NULL, FALSE);
    }
}

void Local_Save(const std::wstring& iniPath)
{
    if (!g_page) {
        return;
    }
    const std::string f = narrow(iniPath);
    WritePrivateProfileStringA("Local", NULL, NULL, f.c_str()); // rewrite the section
    WritePrivateProfileStringA("Local", "Count", std::to_string(g_pl.size()).c_str(), f.c_str());
    // the rects follow the fixed layouts; the game uses them while the player count matches
    WritePrivateProfileStringA("Local", "Custom", "1", f.c_str());
    WritePrivateProfileStringA("Local", "Split2", g_sideBySide ? "sidebyside" : "topbottom", f.c_str());
    for (size_t i = 0; i < g_pl.size(); i++) {
        const Player& p = g_pl[i];
        char key[32], val[96];
        snprintf(key, sizeof(key), "P%d", (int)i + 1);
        WritePrivateProfileStringA("Local", key, p.id.c_str(), f.c_str());
        snprintf(key, sizeof(key), "P%dName", (int)i + 1);
        WritePrivateProfileStringA("Local", key, narrow(p.name).c_str(), f.c_str());
        snprintf(key, sizeof(key), "P%dChar", (int)i + 1);
        WritePrivateProfileStringA("Local", key, i == 0 ? "default" : p.ch.c_str(), f.c_str()); // P1 = Character tab
        snprintf(key, sizeof(key), "P%dSeat", (int)i + 1);
        WritePrivateProfileStringA("Local", key, std::to_string(p.seat).c_str(), f.c_str());
        snprintf(key, sizeof(key), "P%dRect", (int)i + 1);
        snprintf(val, sizeof(val), "%.4f,%.4f,%.4f,%.4f", p.x, p.y, p.w, p.h);
        WritePrivateProfileStringA("Local", key, val, f.c_str());
    }
    // controllers that are off now keep their character for next time (the game ignores these)
    int n = 0;
    for (const Player& p : g_known) {
        if (n >= 16 || std::any_of(g_pl.begin(), g_pl.end(), [&](const Player& q) { return q.id == p.id; })) {
            continue;
        }
        char key[32];
        snprintf(key, sizeof(key), "Known%d", ++n);
        WritePrivateProfileStringA("Local", key, (p.id + "|" + p.ch + "|" + narrow(p.name)).c_str(), f.c_str());
    }
}
