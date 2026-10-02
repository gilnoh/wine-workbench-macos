/* Decode an Ogg Vorbis file to PCM with an APPLICATION'S OWN vorbisfile.dll and
 * report how long that takes against the audio's duration.
 *
 *   oggprobe32.exe <dir containing vorbisfile.dll> <file.ogg> [out.pcm]
 *
 * With out.pcm, the decoded 16-bit little-endian PCM is written there, so two
 * decoders can be compared sample by sample.
 *
 * Written for Strategic Command WWII, whose vorbis.dll is pure x87 (3013 x87
 * float instructions, 0 SSE) and which decodes music on its main thread. Run the
 * same binary, DLL and file in two environments (ours, CrossOver) to tell
 * "Rosetta's x87 emulation" apart from "something in our runtime".
 *
 * ov_open_callbacks with our own I/O, so no FILE* crosses a CRT boundary. */
#include <windows.h>
#include <stdio.h>

typedef struct {
    size_t (*read_func)(void *ptr, size_t size, size_t nmemb, void *datasource);
    int    (*seek_func)(void *datasource, long long offset, int whence);
    int    (*close_func)(void *datasource);
    long   (*tell_func)(void *datasource);
} ov_callbacks;
typedef struct { int version, channels; long rate; } vorbis_info_head;

typedef int  (*p_ov_open_callbacks)(void *, void *, const char *, long, ov_callbacks);
typedef long (*p_ov_read)(void *, char *, int, int, int, int, int *);
typedef vorbis_info_head *(*p_ov_info)(void *, int);
typedef int  (*p_ov_clear)(void *);

static size_t rd(void *p, size_t s, size_t n, void *f) { return fread(p, s, n, (FILE *)f); }
static int    sk(void *f, long long o, int w)          { return fseek((FILE *)f, (long)o, w); }
static int    cl(void *f)                              { return fclose((FILE *)f); }
static long   tl(void *f)                              { return ftell((FILE *)f); }

int main(int argc, char **argv)
{
    static char vf[4096];               /* OggVorbis_File, opaque; generously sized */
    static char buf[65536];
    ov_callbacks cb = { rd, sk, cl, tl };
    LARGE_INTEGER f, t0, t1;
    long long bytes = 0;
    vorbis_info_head *vi;
    HMODULE h;
    FILE *fp;
    int sec, r;
    long n;

    if (argc < 3) { printf("usage: oggprobe <dll dir> <file.ogg> [out.pcm]\n"); return 2; }
    SetDllDirectoryA(argv[1]);
    { char p[MAX_PATH]; snprintf(p, sizeof(p), "%s\\vorbisfile.dll", argv[1]); h = LoadLibraryA(p); }
    if (!h) { printf("LoadLibrary vorbisfile.dll failed: %lu\n", GetLastError()); return 1; }
    p_ov_open_callbacks ov_open_cb = (p_ov_open_callbacks)GetProcAddress(h, "ov_open_callbacks");
    p_ov_read ov_read = (p_ov_read)GetProcAddress(h, "ov_read");
    p_ov_info ov_info = (p_ov_info)GetProcAddress(h, "ov_info");
    p_ov_clear ov_clear = (p_ov_clear)GetProcAddress(h, "ov_clear");
    if (!ov_open_cb || !ov_read || !ov_info || !ov_clear) { printf("missing exports\n"); return 1; }

    if (!(fp = fopen(argv[2], "rb"))) { printf("cannot open %s\n", argv[2]); return 1; }
    if ((r = ov_open_cb(fp, vf, NULL, 0, cb))) { printf("ov_open_callbacks failed: %d\n", r); return 1; }
    vi = ov_info(vf, -1);
    {
        const char *(CDECL *wgv)(void) = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version");
        const char *(CDECL *wgb)(void) = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_build_id");
        printf("wine: %s  build: %s\n", wgv ? wgv() : "?", wgb ? wgb() : "?");
    }
    printf("bits: %d  channels=%d rate=%ld\n", (int)sizeof(void *) * 8, vi->channels, vi->rate);

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    {
        FILE *out = argc > 3 ? fopen(argv[3], "wb") : NULL;
        while ((n = ov_read(vf, buf, sizeof(buf), 0, 2, 1, &sec)) > 0)
        {
            bytes += n;
            if (out) fwrite(buf, 1, n, out);
        }
        if (out) fclose(out);
    }
    QueryPerformanceCounter(&t1);
    {
        double wall = (t1.QuadPart - t0.QuadPart) / (double)f.QuadPart;
        double audio = bytes / (double)(vi->channels * 2 * vi->rate);
        printf("decoded %.1f s of audio in %.2f s wall  (%.1fx realtime)\n", audio, wall, audio / wall);
    }
    ov_clear(vf);
    return 0;
}
