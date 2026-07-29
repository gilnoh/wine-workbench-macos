/* Rasterise a glyph through DirectWrite exactly the way Skia does.
 *
 * Chromium's renderer stops dead after inspecting fonts: it never calls
 * CreateGlyphRunAnalysis, while the same Steam build under CrossOver's Wine
 * calls it 373 times. This asks our Wine directly whether that path works at
 * all, independently of Chromium.
 *
 * Prints, for one glyph of one font:
 *   - whether IDWriteFactory2 is available
 *   - whether CreateGlyphRunAnalysis succeeds
 *   - the alpha texture bounds it reports
 *   - how many texture bytes are non-zero (0 = a blank glyph)
 */
#include <windows.h>
#include <dwrite_2.h>
#include <cstdio>
#include <vector>

static const WCHAR *FAMILY = L"Arial";

int wmain(void)
{
    IDWriteFactory *factory = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown **>(&factory));
    wprintf(L"DWriteCreateFactory            hr=0x%08lx\n", hr);
    if (FAILED(hr)) return 1;

    IDWriteFactory2 *factory2 = nullptr;
    hr = factory->QueryInterface(__uuidof(IDWriteFactory2), (void **)&factory2);
    wprintf(L"QueryInterface IDWriteFactory2 hr=0x%08lx  %s\n", hr, SUCCEEDED(hr) ? L"available" : L"MISSING");
    if (FAILED(hr)) return 2;

    IDWriteFontCollection *coll = nullptr;
    if (FAILED(hr = factory->GetSystemFontCollection(&coll, TRUE))) return 3;

    UINT32 index = 0; BOOL exists = FALSE;
    coll->FindFamilyName(FAMILY, &index, &exists);
    wprintf(L"FindFamilyName(%ls)           exists=%d\n", FAMILY, exists);
    if (!exists) return 4;

    IDWriteFontFamily *family = nullptr;
    IDWriteFont *font = nullptr;
    IDWriteFontFace *face = nullptr;
    coll->GetFontFamily(index, &family);
    family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL, &font);
    hr = font->CreateFontFace(&face);
    wprintf(L"CreateFontFace                 hr=0x%08lx\n", hr);
    if (FAILED(hr)) return 5;

    /* glyph run for a single 'A' at 16px */
    UINT32 cp = L'A';
    UINT16 glyph = 0;
    face->GetGlyphIndices(&cp, 1, &glyph);
    FLOAT advance = 0.0f;
    DWRITE_GLYPH_OFFSET offset = {0.0f, 0.0f};
    wprintf(L"glyph index for 'A'            %u\n", glyph);

    DWRITE_GLYPH_RUN run = {};
    run.fontFace = face;
    run.fontEmSize = 16.0f;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = &advance;
    run.glyphOffsets = &offset;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    IDWriteGlyphRunAnalysis *analysis = nullptr;
    hr = factory2->CreateGlyphRunAnalysis(&run, nullptr,
                                          DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                          DWRITE_MEASURING_MODE_NATURAL,
                                          DWRITE_GRID_FIT_MODE_DEFAULT,
                                          DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                                          0.0f, 0.0f, &analysis);
    wprintf(L"CreateGlyphRunAnalysis         hr=0x%08lx\n", hr);
    if (FAILED(hr) || !analysis) return 6;

    for (int pass = 0; pass < 2; pass++)
    {
        DWRITE_TEXTURE_TYPE type = pass ? DWRITE_TEXTURE_CLEARTYPE_3x1 : DWRITE_TEXTURE_ALIASED_1x1;
        RECT r = {0, 0, 0, 0};
        hr = analysis->GetAlphaTextureBounds(type, &r);
        int w = r.right - r.left, h = r.bottom - r.top;
        wprintf(L"GetAlphaTextureBounds(%s) hr=0x%08lx bounds=(%ld,%ld)-(%ld,%ld) %dx%d\n",
                pass ? L"3x1    " : L"1x1    ", hr, r.left, r.top, r.right, r.bottom, w, h);
        if (FAILED(hr) || w <= 0 || h <= 0) continue;

        size_t size = (size_t)w * h * (pass ? 3 : 1);
        std::vector<BYTE> buf(size, 0);
        hr = analysis->CreateAlphaTexture(type, &r, buf.data(), (UINT32)size);
        size_t nz = 0;
        for (size_t i = 0; i < size; i++) if (buf[i]) nz++;
        wprintf(L"CreateAlphaTexture(%s)    hr=0x%08lx  non-zero bytes = %zu / %zu %s\n",
                pass ? L"3x1" : L"1x1", hr, nz, size, nz ? L"<- GLYPH RENDERED" : L"<- BLANK");
    }

    analysis->Release();
    face->Release(); font->Release(); family->Release();
    coll->Release(); factory2->Release(); factory->Release();
    return 0;
}
