#include <windows.h>
#include <gdiplus.h>
#include <fstream>
#include <vector>
#include <iostream>

using namespace Gdiplus;
#pragma comment(lib, "gdiplus.lib")

// -------------------- Shared Tint Function --------------------
#include <algorithm>
#include <cmath>

inline void TintPixels(
    BYTE* pixels,
    UINT width,
    UINT height,
    INT  stride)
{
    const float lowHue = 15.0f;   // lower bound of “orange”
    const float highHue = 45.0f;   // upper bound of “orange”
    const float newHue = 195.0f;   // DeepSkyBlue ≈195°

    for (UINT y = 0; y < height; y++) {
        BYTE* row = pixels + y * stride;

        for (UINT x = 0; x < width; x++) {
            BYTE* px = row + 4 * x;     // BGRA

            if (px[3] == 0)            // skip fully transparent
                continue;

            // 1) RGB → [0,1]
            float r = px[2] / 255.0f;
            float g = px[1] / 255.0f;
            float b = px[0] / 255.0f;

            // 2) RGB → HSL
            float maxc = std::max(r, std::max(g, b));
            float minc = std::min(r, std::min(g, b));
            float L = (maxc + minc) * 0.5f;
            float d = maxc - minc;

            float H = 0, S = 0;
            if (d > 1e-6f) {
                S = d / (1 - fabsf(2 * L - 1));
                if (maxc == r) H = std::fmodf((g - b) / d, 6.0f);
                else if (maxc == g) H = ((b - r) / d) + 2.0f;
                else                H = ((r - g) / d) + 4.0f;
                H *= 60.0f;
                if (H < 0) H += 360.0f;
            }

            // 3) If it’s in the orange band, switch hue → DeepSkyBlue
            if (H < lowHue || H > highHue)
                continue;
            H = newHue;

            // 4) HSL → RGB
            float C = (1 - fabsf(2 * L - 1)) * S;
            float X = C * (1 - fabsf(std::fmodf(H / 60.0f, 2.0f) - 1));
            float m = L - C / 2.0f;

            float rp = 0, gp = 0, bp = 0;
            if (H < 60) { rp = C;  gp = X;  bp = 0; }
            else if (H < 120) { rp = X;  gp = C;  bp = 0; }
            else if (H < 180) { rp = 0;  gp = C;  bp = X; }
            else if (H < 240) { rp = 0;  gp = X;  bp = C; }
            else if (H < 300) { rp = X;  gp = 0;  bp = C; }
            else { rp = C;  gp = 0;  bp = X; }

            // 5) Write back, clamp, preserve alpha
            px[2] = (BYTE)std::clamp((rp + m) * 255.0f, 0.0f, 255.0f);
            px[1] = (BYTE)std::clamp((gp + m) * 255.0f, 0.0f, 255.0f);
            px[0] = (BYTE)std::clamp((bp + m) * 255.0f, 0.0f, 255.0f);
        }
    }
}

// -------------------- Helpers --------------------
struct ICONDIR {
    WORD idReserved;
    WORD idType;
    WORD idCount;
};

struct ICONDIRENTRY {
    BYTE bWidth;
    BYTE bHeight;
    BYTE bColorCount;
    BYTE bReserved;
    WORD wPlanes;
    WORD wBitCount;
    DWORD dwBytesInRes;
    DWORD dwImageOffset;
};

std::vector<BYTE> ReadFileBytes(const wchar_t* path) {
    std::ifstream ifs(path, std::ios::binary);
    return std::vector<BYTE>((std::istreambuf_iterator<char>(ifs)),
        std::istreambuf_iterator<char>());
}

bool IsPNG(const BYTE* data, size_t size) {
    if (size < 8) return false;
    static const BYTE sig[8] = { 137,80,78,71,13,10,26,10 };
    return memcmp(data, sig, 8) == 0;
}

CLSID GetEncoderClsid(const WCHAR* format) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    std::vector<BYTE> buf(size);
    ImageCodecInfo* info = (ImageCodecInfo*)buf.data();
    GetImageEncoders(num, size, info);
    for (UINT j = 0; j < num; j++) {
        if (wcscmp(info[j].MimeType, format) == 0)
            return info[j].Clsid;
    }
    return CLSID();
}

// -------------------- Tint PNG --------------------
bool ProcessPNG(const wchar_t* input, const wchar_t* output) {
    Bitmap bmp(input);
    if (bmp.GetLastStatus() != Ok) return false;

    Rect rect(0, 0, bmp.GetWidth(), bmp.GetHeight());
    BitmapData data;
    bmp.LockBits(&rect, ImageLockModeRead | ImageLockModeWrite,
        PixelFormat32bppARGB, &data);

    TintPixels((BYTE*)data.Scan0, bmp.GetWidth(), bmp.GetHeight(), data.Stride);

    bmp.UnlockBits(&data);

    CLSID pngClsid = GetEncoderClsid(L"image/png");
    return bmp.Save(output, &pngClsid) == Ok;
}

// -------------------- Tint ICO --------------------
bool ProcessICO(const wchar_t* input, const wchar_t* output) {
    // Read the entire ICO file
    auto bytes = ReadFileBytes(input);
    if (bytes.size() < sizeof(ICONDIR)) return false;

    ICONDIR* dir = (ICONDIR*)bytes.data();
    if (dir->idType != 1) return false;

    ICONDIRENTRY* entries = (ICONDIRENTRY*)(bytes.data() + sizeof(ICONDIR));
    std::vector<std::vector<BYTE>> newImages;
    CLSID pngClsid = GetEncoderClsid(L"image/png");

    for (int i = 0; i < dir->idCount; i++) {
        ICONDIRENTRY& e = entries[i];
        BYTE* imgData = bytes.data() + e.dwImageOffset;
        size_t imgSize = e.dwBytesInRes;

        std::vector<BYTE> raw(imgData, imgData + imgSize);
        Bitmap* bmp = nullptr;

        // 1) If it's a PNG chunk, load directly from a GDI+ stream
        if (IsPNG(raw.data(), raw.size())) {
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, raw.size());
            void* pMem = GlobalLock(hMem);
            memcpy(pMem, raw.data(), raw.size());
            GlobalUnlock(hMem);

            IStream* pStream = nullptr;
            CreateStreamOnHGlobal(hMem, TRUE, &pStream);
            bmp = new Bitmap(pStream);
            pStream->Release();
            // hMem is auto-freed because we passed TRUE
        }
        // 2) Otherwise it's a DIB  AND mask: use CreateIconFromResourceEx
        else {
            HICON hIcon = CreateIconFromResourceEx(
                raw.data(),
                static_cast<DWORD>(raw.size()),
                TRUE,                   // icon flag
                0x00030000,             // version = RT_ICON
                0,                       // use native size
                0,
                LR_DEFAULTCOLOR
            );
            if (!hIcon) {
                std::wcerr << L"Failed to create HICON for entry " << i << L"\n";
                return false;
            }

            // Convert HICON -> Gdiplus::Bitmap
            Bitmap* tmp = Bitmap::FromHICON(hIcon);
            DestroyIcon(hIcon);

            if (tmp->GetLastStatus() != Ok) {
                delete tmp;
                std::wcerr << L"Failed to load sub-image " << i << L"\n";
                return false;
            }

            // Ensure we have 32bpp ARGB so LockBits always works
            bmp = tmp->Clone(
                0, 0,
                tmp->GetWidth(),
                tmp->GetHeight(),
                PixelFormat32bppARGB
            );
            delete tmp;
        }

        // Bail if GDI+ Bitmap failed
        if (!bmp || bmp->GetLastStatus() != Ok) {
            delete bmp;
            std::wcerr << L"Failed to load sub-image " << i << L"\n";
            return false;
        }

        // Apply our orange->cyan tint
        Rect rect(0, 0, bmp->GetWidth(), bmp->GetHeight());
        BitmapData data;
        bmp->LockBits(&rect,
            ImageLockModeRead | ImageLockModeWrite,
            PixelFormat32bppARGB,
            &data);

        TintPixels((BYTE*)data.Scan0,
            bmp->GetWidth(),
            bmp->GetHeight(),
            data.Stride);

        bmp->UnlockBits(&data);

        // Save the tinted image back into a PNG stream
        IStream* outStream = nullptr;
        CreateStreamOnHGlobal(NULL, TRUE, &outStream);
        bmp->Save(outStream, &pngClsid);

        // Extract the bytes from the stream
        STATSTG stat;
        outStream->Stat(&stat, STATFLAG_NONAME);
        ULONG newSize = static_cast<ULONG>(stat.cbSize.QuadPart);

        std::vector<BYTE> outBytes(newSize);
        LARGE_INTEGER zero = {};
        outStream->Seek(zero, STREAM_SEEK_SET, nullptr);

        ULONG read = 0;
        outStream->Read(outBytes.data(), newSize, &read);
        outStream->Release();

        delete bmp;

        newImages.push_back(std::move(outBytes));

        // Update directory entry for PNG‐encoded data
        e.dwBytesInRes = newSize;
        e.dwImageOffset = 0;     // will be fixed below
        e.bColorCount = 0;
        e.wPlanes = 1;
        e.wBitCount = 32;
    }

    // Recompute offsets and write out a fresh ICO
    size_t headerSize = sizeof(ICONDIR) + dir->idCount * sizeof(ICONDIRENTRY);
    DWORD offset = static_cast<DWORD>(headerSize);
    for (int i = 0; i < dir->idCount; i++) {
        entries[i].dwImageOffset = offset;
        offset += entries[i].dwBytesInRes;
    }

    std::ofstream ofs(output, std::ios::binary);
    ofs.write(reinterpret_cast<char*>(dir), sizeof(ICONDIR));
    ofs.write(reinterpret_cast<char*>(entries),
        dir->idCount * sizeof(ICONDIRENTRY));

    for (auto& img : newImages) {
        ofs.write(reinterpret_cast<char*>(img.data()), img.size());
    }

    return true;
}

// -------------------- Main --------------------
int wmain(int argc, wchar_t* argv[]) {
    if (argc < 3) {
        std::wcout << L"Usage: tinticon.exe input.(ico|png) output.(ico|png)\n";
        return 0;
    }

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    std::wstring in = argv[1], out = argv[2];
    bool ok = false;
    if (in.size() >= 4 && _wcsicmp(in.c_str() + in.size() - 4, L".png") == 0)
        ok = ProcessPNG(in.c_str(), out.c_str());
    else if (in.size() >= 4 && _wcsicmp(in.c_str() + in.size() - 4, L".ico") == 0)
        ok = ProcessICO(in.c_str(), out.c_str());
    else
        std::wcerr << L"Unsupported extension\n";

    if (!ok) std::wcerr << L"Processing failed\n";

    GdiplusShutdown(gdiplusToken);
    return 0;
}
