/* Enumerate the DirectWrite system font collection, the way Chromium does. */
#include <windows.h>
#include <dwrite.h>
#include <cstdio>

int wmain(void)
{
    IDWriteFactory *factory = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown **>(&factory));
    wprintf(L"DWriteCreateFactory hr=0x%08lx factory=%p\n", hr, factory);
    if (FAILED(hr) || !factory) return 1;

    IDWriteFontCollection *coll = nullptr;
    hr = factory->GetSystemFontCollection(&coll, TRUE);
    wprintf(L"GetSystemFontCollection hr=0x%08lx\n", hr);
    if (FAILED(hr) || !coll) return 2;

    UINT32 n = coll->GetFontFamilyCount();
    wprintf(L"font family count = %u\n", n);

    for (UINT32 i = 0; i < n && i < 12; i++)
    {
        IDWriteFontFamily *fam = nullptr;
        if (FAILED(coll->GetFontFamily(i, &fam))) continue;
        IDWriteLocalizedStrings *names = nullptr;
        if (SUCCEEDED(fam->GetFamilyNames(&names)) && names)
        {
            WCHAR buf[128] = {0};
            names->GetString(0, buf, 128);
            wprintf(L"  [%u] %s\n", i, buf);
            names->Release();
        }
        fam->Release();
    }
    coll->Release();
    factory->Release();
    return 0;
}
