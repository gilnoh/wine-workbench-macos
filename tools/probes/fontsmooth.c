/* Report the font-smoothing parameters Chromium/Skia reads when choosing a
 * text rendering mode. A degenerate value here makes Skia produce no glyph
 * mask at all, which looks exactly like "the app draws everything but text". */
#include <windows.h>
#include <stdio.h>

static void q(const char *name, UINT action)
{
    UINT v = 0xdeadbeef;
    BOOL ok = SystemParametersInfoA(action, 0, &v, 0);
    printf("%-34s ok=%d value=%u (0x%x)\n", name, ok, v, v);
}

int main(void)
{
    BOOL b = 2;
    printf("SPI_GETFONTSMOOTHING               ok=%d value=%d\n",
           SystemParametersInfoA(SPI_GETFONTSMOOTHING, 0, &b, 0), b);
    q("SPI_GETFONTSMOOTHINGTYPE",        SPI_GETFONTSMOOTHINGTYPE);
    q("SPI_GETFONTSMOOTHINGCONTRAST",    SPI_GETFONTSMOOTHINGCONTRAST);
    q("SPI_GETFONTSMOOTHINGORIENTATION", SPI_GETFONTSMOOTHINGORIENTATION);

    HDC dc = GetDC(NULL);
    printf("LOGPIXELSX=%d LOGPIXELSY=%d BITSPIXEL=%d\n",
           GetDeviceCaps(dc, LOGPIXELSX), GetDeviceCaps(dc, LOGPIXELSY),
           GetDeviceCaps(dc, BITSPIXEL));
    ReleaseDC(NULL, dc);
    return 0;
}
