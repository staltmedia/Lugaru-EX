// LugaruEX launcher - Controls tab (GPLv2+)
#include "controls.h"

#include "padviz.h"
#include "resource.h"

#include <commctrl.h>
#include <shlwapi.h>

#include <algorithm>
#include <map>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace
{
struct Act
{
    const char* name;        // ini key
    const wchar_t* label;    // shown
    const char* kbDef;       // default keyboard/mouse (SDL name); nullptr = fixed
    const char* padDef;      // default pad button; nullptr = fixed
    const wchar_t* kbFixed;  // text when not rebindable
    const wchar_t* padFixed;
};

const Act kActs[] = {
    { "Forward", L"Move forward", "W", nullptr, nullptr, L"Left stick" },
    { "Back", L"Move back", "S", nullptr, nullptr, L"Left stick" },
    { "Left", L"Move left", "A", nullptr, nullptr, L"Left stick" },
    { "Right", L"Move right", "D", nullptr, nullptr, L"Left stick" },
    { "Jump", L"Jump", "Space", "a", nullptr, nullptr },
    { "Crouch", L"Crouch / roll / block", "Left Shift", "lefttrigger", nullptr, nullptr },
    { "Attack", L"Attack", "Mouse Left", "righttrigger", nullptr, nullptr },
    { "Draw", L"Draw / put away", "E", "y", nullptr, nullptr },
    { "Throw", L"Throw / pick up", "Q", "x", nullptr, nullptr },
    { "Lighting", L"Shaders (next look)", "R", "back", nullptr, nullptr },
    { "Pause", L"Pause menu", nullptr, "start", L"Esc", nullptr },
    { "Console", L"Console", "`", nullptr, nullptr, L"-" },
    { "Look", L"Look around", nullptr, nullptr, L"Mouse", L"Right stick" },
};
const int kNumActs = sizeof(kActs) / sizeof(kActs[0]);

HWND g_page = NULL, g_list = NULL;
std::wstring g_exeDir, g_presetDir;
std::map<std::string, std::string> g_kb, g_pad; // action -> binding name
bool g_vibration = true;
int g_look = 100; // look speed %
int g_capRow = -1, g_capCol = 0; // capture state (col 1 keyboard, 2 pad)
std::string g_vizPad; // id of the controller shown in the picture

std::wstring widen(const std::string& s)
{
    return std::wstring(s.begin(), s.end());
}
std::string narrow(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w) {
        s += (c < 128) ? (char)c : '_';
    }
    return s;
}

std::wstring kbLabel(const std::string& n)
{
    if (n.empty()) {
        return L"(none)";
    }
    if (n == "`") {
        return L"` (tilde key)";
    }
    return widen(n);
}

void setDefaults()
{
    for (const Act& a : kActs) {
        g_kb[a.name] = a.kbDef ? a.kbDef : "";
        g_pad[a.name] = a.padDef ? a.padDef : "";
    }
    g_vibration = true;
    g_look = 100;
}

void readFrom(const std::wstring& file, bool keepMissing)
{
    char buf[128];
    const std::string f = narrow(file);
    for (const Act& a : kActs) {
        if (a.kbDef) {
            GetPrivateProfileStringA("Keys", a.name, keepMissing ? g_kb[a.name].c_str() : a.kbDef, buf, sizeof(buf), f.c_str());
            g_kb[a.name] = buf;
        }
        if (a.padDef) {
            GetPrivateProfileStringA("Pad", a.name, keepMissing ? g_pad[a.name].c_str() : a.padDef, buf, sizeof(buf), f.c_str());
            g_pad[a.name] = buf;
        }
    }
    // the first LugaruEX defaults (B crouch, X attack, RB throw) were never chosen
    // by anyone: move untouched setups to the current defaults
    if (g_pad["Jump"] == "a" && g_pad["Crouch"] == "b" && g_pad["Attack"] == "x" && g_pad["Draw"] == "y" && g_pad["Throw"] == "rightshoulder") {
        for (const Act& a : kActs) {
            if (a.padDef) {
                g_pad[a.name] = a.padDef;
            }
        }
    }
    g_vibration = GetPrivateProfileIntA("Pad", "Vibration", 1, f.c_str()) != 0;
    GetPrivateProfileStringA("Pad", "LookSpeed", "", buf, sizeof(buf), f.c_str());
    if (buf[0]) {
        g_look = std::max(20, std::min(300, (int)(atof(buf) * 100 + 0.5)));
    }
}

void writeTo(const std::wstring& file)
{
    const std::string f = narrow(file);
    for (const Act& a : kActs) {
        if (a.kbDef) {
            WritePrivateProfileStringA("Keys", a.name, g_kb[a.name].c_str(), f.c_str());
        }
        if (a.padDef) {
            WritePrivateProfileStringA("Pad", a.name, g_pad[a.name].c_str(), f.c_str());
        }
    }
    WritePrivateProfileStringA("Pad", "Vibration", g_vibration ? "1" : "0", f.c_str());
    WritePrivateProfileStringA("Pad", "InvertY", NULL, f.c_str()); // invert look was removed
    char b[32];
    snprintf(b, sizeof(b), "%.2f", g_look / 100.f);
    WritePrivateProfileStringA("Pad", "LookSpeed", b, f.c_str());
}

void refreshList()
{
    for (int i = 0; i < kNumActs; i++) {
        const Act& a = kActs[i];
        std::wstring kb = a.kbDef ? kbLabel(g_kb[a.name]) : a.kbFixed;
        std::wstring pd = a.padDef ? Pad_CodeLabel(Pad_CodeFromName(g_pad[a.name])) : a.padFixed;
        if (i == g_capRow) {
            (g_capCol == 1 ? kb : pd) = L"press...";
        }
        ListView_SetItemText(g_list, i, 1, (LPWSTR)kb.c_str());
        ListView_SetItemText(g_list, i, 2, (LPWSTR)pd.c_str());
    }
}

void hint(const std::wstring& s)
{
    SetDlgItemTextW(g_page, IDC_CT_HINT, s.c_str());
}

void defaultHint()
{
    hint(L""); // only rebinding prompts show here
}

void endCapture()
{
    g_capRow = -1;
    refreshList();
    defaultHint();
}

void assign(int row, int col, const std::string& value)
{
    auto& m = (col == 1) ? g_kb : g_pad;
    const std::string name = kActs[row].name;
    const std::string old = m[name];
    for (const Act& a : kActs) { // swap with an action already using it
        if (a.name != name && m.count(a.name) && m[a.name] == value && ((col == 1) ? a.kbDef : a.padDef)) {
            m[a.name] = old;
        }
    }
    m[name] = value;
    endCapture();
}

void fillPresets(const std::wstring& select)
{
    HWND c = GetDlgItem(g_page, IDC_CT_PRESET);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Default");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_presetDir + L"*.ini").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)n.substr(0, n.size() - 4).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    SetWindowTextW(c, select.c_str());
}

std::wstring presetName()
{
    wchar_t b[128] = {};
    GetDlgItemTextW(g_page, IDC_CT_PRESET, b, 128);
    std::wstring n = b;
    std::wstring clean;
    for (wchar_t ch : n) {
        if (!wcschr(L"\\/:*?\"<>|", ch)) {
            clean += ch;
        }
    }
    while (!clean.empty() && clean.back() == L' ') {
        clean.pop_back();
    }
    return clean;
}

void syncLookControls()
{
    CheckDlgButton(g_page, IDC_CT_VIBRATION, g_vibration ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(g_page, IDC_CT_LOOK, TBM_SETPOS, TRUE, g_look);
    SetDlgItemTextW(g_page, IDC_CT_LOOKLBL, (std::to_wstring(g_look) + L"%").c_str());
}

LRESULT CALLBACK ListProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (g_capRow >= 0 && msg == WM_GETDLGCODE) {
        return DLGC_WANTALLKEYS; // keep Esc/Enter/Tab away from the dialog while capturing
    }
    if (g_capRow >= 0 && g_capCol == 1) {
        switch (msg) {
            case WM_GETDLGCODE:
                return DLGC_WANTALLKEYS;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                if (wp == VK_ESCAPE) {
                    endCapture();
                    return 0;
                }
                std::string n = Key_NameFromMessage(wp, lp);
                if (!n.empty()) {
                    assign(g_capRow, 1, n);
                }
                return 0;
            }
            case WM_CHAR:
            case WM_SYSCHAR:
            case WM_KEYUP:
            case WM_SYSKEYUP:
                return 0;
            case WM_LBUTTONDOWN:
                assign(g_capRow, 1, "Mouse Left");
                return 0;
            case WM_RBUTTONDOWN:
                assign(g_capRow, 1, "Mouse Right");
                return 0;
            case WM_MBUTTONDOWN:
                assign(g_capRow, 1, "Mouse Middle");
                return 0;
            case WM_XBUTTONDOWN:
                assign(g_capRow, 1, GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? "Mouse 4" : "Mouse 5");
                return TRUE;
        }
    }
    if (g_capRow >= 0 && g_capCol == 2 && (msg == WM_KEYDOWN && wp == VK_ESCAPE)) {
        endCapture();
        return 0;
    }
    return DefSubclassProc(w, msg, wp, lp);
}

LRESULT CALLBACK VizProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT rc;
        GetClientRect(w, &rc);
        HDC m = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(m, bmp);
        HBRUSH bg = CreateSolidBrush(RGB(24, 22, 22));
        FillRect(m, &rc, bg);
        DeleteObject(bg);
        RECT inner = { rc.left + 6, rc.top + 6, rc.right - 6, rc.bottom - 6 };
        PadViz_Draw(m, inner, Pad_Find(g_vizPad), RGB(225, 60, 40), true);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, m, 0, 0, SRCCOPY);
        SelectObject(m, ob);
        DeleteObject(bmp);
        DeleteDC(m);
        EndPaint(w, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

void fillPads()
{
    HWND c = GetDlgItem(g_page, IDC_CT_PAD);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    int sel = -1, n = 0;
    for (auto& p : Pad_List()) {
        if (!p.connected) {
            continue;
        }
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)p.name.c_str());
        if (p.id == g_vizPad) {
            sel = n;
        }
        n++;
    }
    if (n == 0) {
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"(no controller connected)");
        sel = 0;
    }
    SendMessageW(c, CB_SETCURSEL, std::max(0, sel), 0);
}
} // namespace

void Controls_Register(HINSTANCE inst)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = VizProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"LugaruEXPadViz";
    RegisterClassW(&wc);
}

void Controls_Init(HWND page, const std::wstring& exeDir, const std::wstring& iniPath)
{
    g_page = page;
    g_exeDir = exeDir;
    g_presetDir = exeDir + L"LugaruEX\\Presets\\";
    CreateDirectoryW((exeDir + L"LugaruEX").c_str(), NULL);
    CreateDirectoryW(g_presetDir.c_str(), NULL);
    setDefaults();
    readFrom(iniPath, true);

    g_list = GetDlgItem(page, IDC_CT_LIST);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    RECT r;
    GetClientRect(g_list, &r);
    const int w = r.right - GetSystemMetrics(SM_CXVSCROLL);
    const wchar_t* cols[] = { L"Action", L"Keyboard / mouse", L"Controller" };
    const int widths[] = { w * 38 / 100, w * 34 / 100, w * 28 / 100 };
    for (int i = 0; i < 3; i++) {
        LVCOLUMNW c = { LVCF_TEXT | LVCF_WIDTH };
        c.pszText = (LPWSTR)cols[i];
        c.cx = widths[i];
        ListView_InsertColumn(g_list, i, &c);
    }
    for (int i = 0; i < kNumActs; i++) {
        LVITEMW it = { LVIF_TEXT };
        it.iItem = i;
        it.pszText = (LPWSTR)kActs[i].label;
        ListView_InsertItem(g_list, &it);
    }
    SetWindowSubclass(g_list, ListProc, 1, 0);
    SendDlgItemMessageW(page, IDC_CT_LOOK, TBM_SETRANGE, TRUE, MAKELPARAM(20, 300));
    syncLookControls();
    fillPresets(L"Default");
    refreshList();
    defaultHint();
    fillPads();
}

INT_PTR Controls_Proc(HWND page, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_NOTIFY: {
            NMHDR* n = (NMHDR*)lp;
            if (n->idFrom == IDC_CT_LIST && n->code == NM_DBLCLK) {
                NMITEMACTIVATE* ia = (NMITEMACTIVATE*)lp;
                if (ia->iItem < 0 || ia->iItem >= kNumActs) {
                    return TRUE;
                }
                const Act& a = kActs[ia->iItem];
                int col = ia->iSubItem;
                if (col == 0) {
                    col = a.kbDef ? 1 : 2;
                }
                if ((col == 1 && !a.kbDef) || (col == 2 && !a.padDef)) {
                    hint(L"That one is fixed and can't be changed.");
                    return TRUE;
                }
                g_capRow = ia->iItem;
                g_capCol = col;
                refreshList();
                SetFocus(g_list);
                hint(col == 1 ? L"Press a key or mouse button for “" + std::wstring(a.label) + L"” (Esc cancels)."
                              : L"Press a button on any controller for “" + std::wstring(a.label) + L"” (Esc cancels).");
                return TRUE;
            }
            break;
        }
        case WM_HSCROLL:
            if ((HWND)lp == GetDlgItem(page, IDC_CT_LOOK)) {
                g_look = (int)SendDlgItemMessageW(page, IDC_CT_LOOK, TBM_GETPOS, 0, 0);
                syncLookControls();
                return TRUE;
            }
            break;
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id == IDC_CT_VIBRATION) {
                g_vibration = IsDlgButtonChecked(page, IDC_CT_VIBRATION) == BST_CHECKED;
                return TRUE;
            }
            if (id == IDC_CT_RESET) {
                setDefaults();
                syncLookControls();
                fillPresets(L"Default");
                endCapture();
                return TRUE;
            }
            if (id == IDC_CT_SAVE) {
                const std::wstring n = presetName();
                if (n.empty() || _wcsicmp(n.c_str(), L"Default") == 0) {
                    MessageBoxW(GetParent(page), L"Type a name for the preset in the box first.", L"LugaruEX", MB_ICONINFORMATION);
                    return TRUE;
                }
                const std::wstring f = g_presetDir + n + L".ini";
                DeleteFileW(f.c_str());
                writeTo(f);
                fillPresets(n);
                hint(L"Saved preset “" + n + L"”.");
                return TRUE;
            }
            if (id == IDC_CT_LOAD || (id == IDC_CT_PRESET && HIWORD(wp) == CBN_SELCHANGE)) {
                HWND c = GetDlgItem(page, IDC_CT_PRESET);
                std::wstring n;
                if (HIWORD(wp) == CBN_SELCHANGE) {
                    const int s = (int)SendMessageW(c, CB_GETCURSEL, 0, 0);
                    wchar_t b[128] = {};
                    SendMessageW(c, CB_GETLBTEXT, s, (LPARAM)b);
                    n = b;
                } else {
                    n = presetName();
                }
                if (_wcsicmp(n.c_str(), L"Default") == 0) {
                    setDefaults();
                } else {
                    const std::wstring f = g_presetDir + n + L".ini";
                    if (!PathFileExistsW(f.c_str())) {
                        MessageBoxW(GetParent(page), (L"No preset called “" + n + L"”.").c_str(), L"LugaruEX", MB_ICONWARNING);
                        return TRUE;
                    }
                    setDefaults();
                    readFrom(f, false);
                }
                syncLookControls();
                endCapture();
                hint(L"Loaded preset “" + n + L"”.");
                return TRUE;
            }
            if (id == IDC_CT_DELETE) {
                const std::wstring n = presetName();
                const std::wstring f = g_presetDir + n + L".ini";
                if (n.empty() || _wcsicmp(n.c_str(), L"Default") == 0 || !PathFileExistsW(f.c_str())) {
                    return TRUE;
                }
                if (MessageBoxW(GetParent(page), (L"Delete the preset “" + n + L"”?").c_str(), L"LugaruEX", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    DeleteFileW(f.c_str());
                    fillPresets(L"Default");
                }
                return TRUE;
            }
            if (id == IDC_CT_PAD && HIWORD(wp) == CBN_SELCHANGE) {
                const int s = (int)SendDlgItemMessageW(page, IDC_CT_PAD, CB_GETCURSEL, 0, 0);
                int n = 0;
                for (auto& p : Pad_List()) {
                    if (p.connected && n++ == s) {
                        g_vizPad = p.id;
                    }
                }
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

void Controls_Tick()
{
    if (!g_page) {
        return;
    }
    // follow the controller that was used last; refresh the list on plug/unplug
    static size_t lastCount = (size_t)-1;
    size_t connected = 0;
    DWORD newest = 0;
    for (auto& p : Pad_List()) {
        if (!p.connected) {
            continue;
        }
        connected++;
        if (p.lastActive > newest) {
            newest = p.lastActive;
            if (GetTickCount() - p.lastActive < 400) {
                g_vizPad = p.id;
            }
        }
    }
    PadState* cur = Pad_Find(g_vizPad);
    if ((!cur || !cur->connected) && connected) {
        for (auto& p : Pad_List()) {
            if (p.connected) {
                g_vizPad = p.id;
                break;
            }
        }
    }
    if (connected != lastCount) {
        lastCount = connected;
        fillPads();
    } else {
        int n = 0;
        for (auto& p : Pad_List()) {
            if (p.connected) {
                if (p.id == g_vizPad) {
                    SendDlgItemMessageW(g_page, IDC_CT_PAD, CB_SETCURSEL, n, 0);
                }
                n++;
            }
        }
    }
    // controller capture
    if (g_capRow >= 0 && g_capCol == 2) {
        for (auto& p : Pad_List()) {
            const int code = Pad_NewPress(p);
            if (code >= 0) {
                assign(g_capRow, 2, Pad_CodeName(code));
                break;
            }
        }
    }
    if (IsWindowVisible(g_page)) {
        InvalidateRect(GetDlgItem(g_page, IDC_CT_VIZ), NULL, FALSE);
    }
}

void Controls_Save(const std::wstring& iniPath)
{
    if (g_page) {
        writeTo(iniPath);
    }
}

bool Controls_Capturing()
{
    return g_capRow >= 0;
}

bool Controls_CapturingKeys()
{
    return g_capRow >= 0 && g_capCol == 1;
}

void Controls_CancelCapture()
{
    if (g_capRow >= 0) {
        endCapture();
    }
}

// a controller's A on the list: change the selected row's controller button (its key
// when the action has no button to set)
void Controls_ActivateRow()
{
    if (!g_list) {
        return;
    }
    int row = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (row < 0 || row >= kNumActs) {
        return;
    }
    const Act& a = kActs[row];
    const int col = a.padDef ? 2 : (a.kbDef ? 1 : 0);
    if (!col) {
        hint(L"That one is fixed and can't be changed.");
        return;
    }
    g_capRow = row;
    g_capCol = col;
    refreshList();
    hint(col == 1 ? L"Press a key or mouse button for “" + std::wstring(a.label) + L"” (Esc or B cancels)."
                  : L"Press a button on any controller for “" + std::wstring(a.label) + L"” (Esc cancels).");
}
