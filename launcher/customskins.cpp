// LugaruEX launcher - player-made skins (GPLv2+)
#include "customskins.h"
#include "../shared/looks.h"

#include <commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>

namespace
{
std::wstring exeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring s = buf;
    return s.substr(0, s.find_last_of(L"\\/") + 1);
}

std::wstring g_ex; // <game>\LugaruEX\ (CustomSkins_SetExDir)
std::wstring exDirW()
{
    return g_ex.empty() ? exeDir() + L"LugaruEX\\" : g_ex;
}

std::wstring folder(const std::string& model)
{
    return exDirW() + L"Data\\Textures\\Custom\\" +(model == "wolf" ? L"wolf" : model == "clothes" ? L"clothes" : L"rabbit");
}

std::wstring wideUtf8(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrowUtf8(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

// any image Windows can read (png, jpg, bmp, gif, tiff, webp...) -> PNG, keeping alpha
bool convertToPng(const std::wstring& src, const std::wstring& dst)
{
    IWICImagingFactory* fac = NULL;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac)))) {
        return false;
    }
    bool ok = false;
    IWICBitmapDecoder* dec = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICFormatConverter* conv = NULL;
    IWICStream* stream = NULL;
    IWICBitmapEncoder* enc = NULL;
    IWICBitmapFrameEncode* out = NULL;
    if (SUCCEEDED(fac->CreateDecoderFromFilename(src.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(fac->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(fac->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(dst.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(fac->CreateEncoder(GUID_ContainerFormatPng, NULL, &enc)) && SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&out, NULL)) && SUCCEEDED(out->Initialize(NULL))) {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        ok = w > 0 && h > 0 && SUCCEEDED(out->SetSize(w, h)) && SUCCEEDED(out->SetPixelFormat(&fmt)) &&
             SUCCEEDED(out->WriteSource(conv, NULL)) && SUCCEEDED(out->Commit()) && SUCCEEDED(enc->Commit());
    }
    for (IUnknown* u : { (IUnknown*)out, (IUnknown*)enc, (IUnknown*)stream, (IUnknown*)conv, (IUnknown*)frame, (IUnknown*)dec, (IUnknown*)fac }) {
        if (u) {
            u->Release();
        }
    }
    if (!ok) {
        DeleteFileW(dst.c_str());
    }
    return ok;
}
} // namespace

std::vector<CustomSkin> CustomSkins_List(const std::vector<std::string>& mods)
{
    // the player's own (LugaruEX\Data\Textures\Custom) and those of the mods that are on
    std::vector<CustomSkin> v;
    for (const LookImage& i : Looks_Images(narrowUtf8(exDirW()), mods)) {
        CustomSkin s;
        s.model = i.model;
        s.enginePath = i.enginePath;
        s.label = wideUtf8(i.label);
        s.file = wideUtf8(i.file);
        std::replace(s.file.begin(), s.file.end(), L'/', L'\\');
        v.push_back(s);
    }
    return v;
}

bool CustomSkins_Import(HWND owner, const std::string& model, CustomSkin& out)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Images (*.png; *.jpg; *.jpeg; *.bmp; *.gif; *.tif; *.tiff; *.webp)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = model == "wolf" ? L"Choose a wolf skin image" : model == "clothes" ? L"Choose a clothes image" : L"Choose a rabbit skin image";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        return false;
    }
    const std::wstring dir = folder(model);
    SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    // file name from the image's name, made safe and unique
    std::wstring name = PathFindFileNameW(file);
    name = name.substr(0, name.rfind(L'.'));
    for (auto& c : name) {
        if (!(c < 128 && (iswalnum(c) || c == L' ' || c == L'-' || c == L'_'))) { // the game opens files by ASCII name
            c = L'_';
        }
    }
    if (name.empty()) {
        name = L"Skin";
    }
    std::wstring dst = dir + L"\\" + name + L".png";
    for (int n = 2; PathFileExistsW(dst.c_str()); n++) {
        dst = dir + L"\\" + name + L" " + std::to_wstring(n) + L".png";
    }
    if (!convertToPng(file, dst)) {
        MessageBoxW(owner, L"That image couldn't be read. Use a PNG, JPG, BMP, GIF, TIFF or WebP image.", L"New skin", MB_ICONWARNING);
        return false;
    }
    for (auto& s : CustomSkins_List()) {
        if (_wcsicmp(s.file.c_str(), dst.c_str()) == 0) {
            out = s;
            return true;
        }
    }
    return false;
}

void CustomSkins_SetExDir(const std::wstring& exDir)
{
    g_ex = exDir;
}

static std::vector<std::string> (*g_modsFn)() = nullptr;

void CustomSkins_SetMods(std::vector<std::string> (*fn)())
{
    g_modsFn = fn;
}

std::vector<CustomSkin> CustomSkins_List()
{
    return CustomSkins_List(g_modsFn ? g_modsFn() : std::vector<std::string>());
}
