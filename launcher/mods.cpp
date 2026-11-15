// LugaruEX launcher - Mods tab (GPLv2+)
#include "mods.h"

#include "resource.h"
#include "../shared/fsutil.h"
#include "../shared/modpack.h"
#include "../shared/looks.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>

namespace
{
HWND g_page = NULL, g_list = NULL;
std::string g_modsDir; // UTF-8
std::vector<ModEntry> g_order;
std::vector<ModInfo> g_info;
bool g_filling = false;

std::string U8(const std::wstring& w)
{
    if (w.empty()) {
        return "";
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

std::wstring W(const std::string& s)
{
    if (s.empty()) {
        return L"";
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

const ModInfo* infoOf(const std::string& folder)
{
    for (auto& m : g_info) {
        if (m.folder == folder) {
            return &m;
        }
    }
    return nullptr;
}

std::wstring sizeText(uint64_t b)
{
    wchar_t t[32];
    if (b >= 1024 * 1024) {
        swprintf(t, 32, L"%.1f MB", b / (1024.0 * 1024.0));
    } else {
        swprintf(t, 32, L"%.0f KB", b / 1024.0 + 0.5);
    }
    return t;
}

void note(const std::wstring& s)
{
    SetDlgItemTextW(g_page, IDC_MD_NOTE, s.c_str());
}

void defaultNote()
{
    note(g_order.empty() ? L"No mods yet. Drag a mod (its .zip or its unpacked folder) onto this window, or use Add mod... "
                           L"A mod is a zip of game files (Textures, Maps, Sounds, Campaigns...)."
                         : L"Ticked mods are on; the higher one wins when two change the same file. "
                           L"Right-click a mod to delete it. Drag more mods (.zip or folder) onto this window to add them.");
}

void fill(int select)
{
    g_filling = true;
    g_info = Mod_List(g_modsDir);
    g_order = Mod_Order(g_order, g_info);
    ListView_DeleteAllItems(g_list);
    for (int i = 0; i < (int)g_order.size(); i++) {
        const ModInfo* m = infoOf(g_order[i].folder);
        std::wstring name = W(m ? m->name : g_order[i].folder);
        LVITEMW it = { LVIF_TEXT };
        it.iItem = i;
        it.pszText = (LPWSTR)name.c_str();
        ListView_InsertItem(g_list, &it);
        std::wstring size = m ? sizeText(m->bytes) : L"";
        ListView_SetItemText(g_list, i, 1, (LPWSTR)size.c_str());
        std::wstring by = m ? W(m->version.empty() ? m->author : (m->author.empty() ? m->version : m->version + " - " + m->author)) : L"";
        ListView_SetItemText(g_list, i, 2, (LPWSTR)by.c_str());
        ListView_SetCheckState(g_list, i, g_order[i].on);
    }
    if (select >= 0 && select < (int)g_order.size()) {
        ListView_SetItemState(g_list, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_list, select, FALSE);
    }
    g_filling = false;
    defaultNote();
}

int selected()
{
    return ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
}

void move(int dir)
{
    const int i = selected(), j = i + dir;
    if (i < 0 || j < 0 || j >= (int)g_order.size()) {
        return;
    }
    std::swap(g_order[i], g_order[j]);
    fill(j);
}

void askDelete(int i)
{
    if (i < 0 || i >= (int)g_order.size()) {
        return;
    }
    const ModInfo* m = infoOf(g_order[i].folder);
    const std::wstring name = W(m ? m->name : g_order[i].folder);
    if (MessageBoxW(GetAncestor(g_page, GA_ROOT), (L"Delete the mod “" + name + L"”? Its files are removed.").c_str(), L"LugaruEX",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    if (!Mod_Delete(g_modsDir, g_order[i].folder)) {
        MessageBoxW(GetAncestor(g_page, GA_ROOT), L"Could not delete all of its files (is the game running?).", L"LugaruEX", MB_ICONWARNING);
    }
    fill(std::min(i, (int)g_order.size() - 2));
}

void contextMenu(int i, POINT pt)
{
    if (i < 0) {
        return;
    }
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_MD_TOGGLE, g_order[i].on ? L"Switch off" : L"Switch on");
    AppendMenuW(m, MF_STRING, IDM_MD_OPEN, L"Open its folder");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_MD_DELETE, L"Delete...");
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_page, NULL);
    DestroyMenu(m);
    if (cmd == IDM_MD_TOGGLE) {
        g_order[i].on = !g_order[i].on;
        fill(i);
    } else if (cmd == IDM_MD_DELETE) {
        askDelete(i);
    } else if (cmd == IDM_MD_OPEN) {
        ShellExecuteW(NULL, L"open", W(fsu::Join(g_modsDir, g_order[i].folder)).c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

void addMods()
{
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetAncestor(g_page, GA_ROOT);
    ofn.lpstrFilter = L"Mods (*.zip, *.rar, *.7z)\0*.zip;*.rar;*.7z\0All files\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrTitle = L"Choose mod zip files";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }
    // one file: the full path; several: the folder, then each name
    std::vector<std::wstring> parts;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
        parts.push_back(p);
    }
    std::vector<std::wstring> files;
    if (parts.size() == 1) {
        files = parts;
    } else {
        for (size_t i = 1; i < parts.size(); i++) {
            files.push_back(parts[0] + L"\\" + parts[i]);
        }
    }
    Mods_Install(files);
}
} // namespace

void Mods_Init(HWND page, const std::wstring& exDir, const std::wstring& iniPath)
{
    g_page = page;
    g_list = GetDlgItem(page, IDC_MD_LIST);
    g_modsDir = U8(exDir) + "Mods";
    fsu::MakeDirs(g_modsDir);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    RECT r;
    GetClientRect(g_list, &r);
    const int w = r.right - GetSystemMetrics(SM_CXVSCROLL);
    const wchar_t* cols[] = { L"Mod", L"Size", L"Version / author" };
    const int widths[] = { w * 50 / 100, w * 18 / 100, w * 32 / 100 };
    for (int i = 0; i < 3; i++) {
        LVCOLUMNW c = { LVCF_TEXT | LVCF_WIDTH };
        c.pszText = (LPWSTR)cols[i];
        c.cx = widths[i];
        ListView_InsertColumn(g_list, i, &c);
    }
    char buf[16384] = {};
    GetPrivateProfileStringA("Mods", "List", "", buf, sizeof(buf), U8(iniPath).c_str());
    g_order = Mod_ParseList(buf);
    fill(0);
}

void Mods_Install(const std::vector<std::wstring>& zips)
{
    std::wstring done, failed, notes;
    int first = -1;
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    for (const std::wstring& z : zips) {
        if (z.empty()) {
            continue;
        }
        std::string folder, err, left;
        const std::wstring shown = z.substr(z.find_last_of(L"\\/", z.size() - 2) + 1);
        if (Mod_Install(U8(z), g_modsDir, folder, err, left)) {
            done += (done.empty() ? L"" : L", ") + shown;
            g_order.insert(g_order.begin(), { folder, true });
            first = 0;
            if (!left.empty()) {
                notes += L"\n" + shown + L": " + W(left);
            }
        } else {
            failed += L"\n" + shown + L": " + W(err);
        }
    }
    SetCursor(old);
    fill(first);
    if (!failed.empty()) {
        MessageBoxW(GetAncestor(g_page, GA_ROOT), (L"Some files could not be installed as mods:" + failed).c_str(), L"LugaruEX", MB_ICONWARNING);
    }
    if (!notes.empty()) {
        MessageBoxW(GetAncestor(g_page, GA_ROOT), (L"Installed, with a note:" + notes).c_str(), L"LugaruEX", MB_ICONINFORMATION);
    }
    if (!done.empty()) {
        note(L"Installed: " + done + L". New mods are on and at the top of the list.");
    }
}

void Mods_Save(const std::wstring& iniPath)
{
    if (g_page) {
        WritePrivateProfileStringA("Mods", "List", Mod_FormatList(g_order).c_str(), U8(iniPath).c_str());
    }
}

void Mods_PadToggle()
{
    const int i = selected();
    if (i >= 0 && i < (int)g_order.size()) {
        g_order[i].on = !g_order[i].on;
        fill(i);
    }
}

void Mods_PadDelete()
{
    askDelete(selected());
}

INT_PTR Mods_Proc(HWND page, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_NOTIFY: {
            NMHDR* n = (NMHDR*)lp;
            if (n->idFrom != IDC_MD_LIST) {
                break;
            }
            if (n->code == LVN_ITEMCHANGED && !g_filling) {
                NMLISTVIEW* lv = (NMLISTVIEW*)lp;
                if ((lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_STATEIMAGEMASK) && lv->iItem >= 0 &&
                    lv->iItem < (int)g_order.size()) {
                    g_order[lv->iItem].on = ListView_GetCheckState(g_list, lv->iItem) != 0;
                }
                return TRUE;
            }
            if (n->code == NM_RCLICK) {
                NMITEMACTIVATE* ia = (NMITEMACTIVATE*)lp;
                POINT pt;
                GetCursorPos(&pt);
                contextMenu(ia->iItem, pt);
                return TRUE;
            }
            if (n->code == LVN_KEYDOWN && ((NMLVKEYDOWN*)lp)->wVKey == VK_DELETE) {
                askDelete(selected());
                return TRUE;
            }
            break;
        }
        case WM_CONTEXTMENU:
            if ((HWND)wp == g_list && lp == (LPARAM)-1) {
                // keyboard menu key: under the selected row
                RECT rc;
                const int i = selected();
                if (i >= 0 && ListView_GetItemRect(g_list, i, &rc, LVIR_LABEL)) {
                    POINT pt = { rc.left, rc.bottom };
                    ClientToScreen(g_list, &pt);
                    contextMenu(i, pt);
                }
                return TRUE;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_MD_ADD:
                    addMods();
                    return TRUE;
                case IDC_MD_UP:
                    move(-1);
                    return TRUE;
                case IDC_MD_DOWN:
                    move(1);
                    return TRUE;
                case IDC_MD_FOLDER:
                    ShellExecuteW(NULL, L"open", W(g_modsDir).c_str(), NULL, NULL, SW_SHOWNORMAL);
                    return TRUE;
            }
            break;
    }
    (void)page;
    return FALSE;
}

std::vector<std::string> Mods_Enabled(const std::wstring& exDir, const std::wstring& iniPath)
{
    if (!g_page) { // (the Mods tab isn't made yet: what the ini says)
        char buf[16384] = {};
        GetPrivateProfileStringA("Mods", "List", "", buf, sizeof(buf), U8(iniPath).c_str());
        return Looks_EnabledMods(U8(exDir), buf);
    }
    std::vector<std::string> out;
    for (const ModEntry& e : g_order) {
        if (e.on) {
            out.push_back(e.folder);
        }
    }
    return out;
}
