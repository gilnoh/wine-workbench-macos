/* Time how long an image takes to decode through GDI+ (GdipLoadImageFromFile +
 * forcing the pixels with GdipBitmapLockBits) -- the path Strategic Command uses
 * for its menu backgrounds.
 *
 *   imgprobe.exe <file> [repeats]
 *
 * Build 32-bit AND 64-bit: the game is 32-bit, and comparing the two separates
 * "this decoder is slow" from "32-bit code under Rosetta is slow". */
#include <windows.h>
#include <stdio.h>

typedef struct { UINT32 GdiplusVersion; void *cb; BOOL a, b; } GdiplusStartupInput;
typedef struct { INT X, Y, Width, Height; } GpRect;
typedef struct { UINT Width, Height; INT Stride; INT PixelFormat; void *Scan0; UINT_PTR Reserved; } BitmapData;
int WINAPI GdiplusStartup(ULONG_PTR *, const GdiplusStartupInput *, void *);
int WINAPI GdipLoadImageFromFile(const WCHAR *, void **);
int WINAPI GdipGetImageWidth(void *, UINT *);
int WINAPI GdipGetImageHeight(void *, UINT *);
int WINAPI GdipBitmapLockBits(void *, const GpRect *, UINT, INT, BitmapData *);
int WINAPI GdipBitmapUnlockBits(void *, BitmapData *);
int WINAPI GdipDisposeImage(void *);

int wmain(int argc, WCHAR **argv)
{
    GdiplusStartupInput in = { 1, NULL, FALSE, FALSE };
    LARGE_INTEGER f, t0, t1, t2;
    ULONG_PTR token;
    int i, n = argc > 2 ? _wtoi(argv[2]) : 1;

    if (argc < 2) { printf("usage: imgprobe <file> [repeats]\n"); return 2; }
    printf("bits: %d\n", (int)sizeof(void *) * 8);
    GdiplusStartup(&token, &in, NULL);
    QueryPerformanceFrequency(&f);
    for (i = 0; i < n; i++)
    {
        void *img = NULL; UINT w = 0, h = 0; int st, lk = -1;
        BitmapData bd; GpRect r;
        QueryPerformanceCounter(&t0);
        st = GdipLoadImageFromFile(argv[1], &img);
        QueryPerformanceCounter(&t1);
        if (!st)
        {
            GdipGetImageWidth(img, &w); GdipGetImageHeight(img, &h);
            r.X = r.Y = 0; r.Width = w; r.Height = h;
            lk = GdipBitmapLockBits(img, &r, 1 /* ImageLockModeRead */, 0x26200A /* 32bppARGB */, &bd);
            if (!lk) GdipBitmapUnlockBits(img, &bd);
        }
        QueryPerformanceCounter(&t2);
        printf("run %d: status=%d %ux%u load=%.1f ms lock(st=%d)=%.1f ms\n", i, st, w, h,
               (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart, lk,
               (t2.QuadPart - t1.QuadPart) * 1000.0 / f.QuadPart);
        if (img) GdipDisposeImage(img);
    }
    return 0;
}
