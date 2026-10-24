/* Is the thread's last error mirrored where x64 code may read it directly?
 *
 *   gsprobe.exe [threads] [hold_ms] (default 4 worker threads, plus main)
 *
 * hold_ms keeps the process alive at the end, e.g. to check with lsof which
 * ntdll.so a launcher actually loaded.
 *
 * On Windows x64, %gs points at the TEB and the last error lives at gs:0x68.
 * Some code reads it there instead of calling GetLastError() -- Unity's Mono
 * does. On macOS, Wine cannot point %gs at the TEB (it is the pthread TSD), and
 * CrossOver mirrors only a few TEB fields into it. Without wine-0008, gs:0x68
 * holds something unrelated, Mono misreads failed file checks, and Sea Power's
 * missions load without ships (docs/NOTEBOOK.md, 2026-10-10 and 2026-10-24).
 *
 * Each check makes an API set the last error, then reads gs:0x68 twice -- from
 * this exe's code and from generated code (a stub in fresh executable memory,
 * as a JIT makes) -- and compares both with GetLastError() (the TEB field), on
 * the main thread and on every worker thread: each thread has its own TSD, and
 * the game failed on its loading threads. PASS: all agree. FAIL: any differs.
 * Exit 0 / 1.
 *
 * Both read sites are needed. Measured 2026-10-24 on stock CrossOver 26.3.0:
 * reads from the exe were right in 25/25 checks, reads from generated code
 * wrong in 25/25 (a pointer-like value on the main thread, 0 on workers).
 * CrossOver registers every native PE module with Apple's libd3dshared as a
 * "non-native code region" (CW HACK 22434), and inside those regions the read
 * comes out right; how is not visible to us. Generated code is never
 * registered. A probe reading only from its own code passes on stock CrossOver
 * -- the first version of this one did. With the drop-in and in the workbench
 * runtime (wine-0008), both read sites pass.
 *
 * Which checks differ narrows down what is missing (inferred from where
 * wine-0008 puts the mirror):
 *   all of them        no mirror at all (stock CrossOver)
 *   file APIs only     ntdll mirrors, but kernelbase.dll lacks wine-0008's
 *                      inline SetLastError mirror -- the state that gave Sea
 *                      Power a grey screen on 2026-10-24
 * On real Windows and on upstream Wine, gs:0x68 is the TEB field itself, so
 * this passes trivially.
 *
 * Rebuilt 2026-10-24 from the throwaway gsprobe.c of 2026-10-10. Under CrossOver,
 * run it through cmd with a redirect: CrossOver's launcher drops stdout. In a
 * workbench prefix, pass wine the macOS path of the exe, not a Z:\ path built
 * with echo -- zsh's echo eats the backslashes and wine then opens Z: in an
 * Explorer window. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXT   16
#define CHECKS 5

static const char *names[CHECKS] = {
    "SetLastError(n)",
    "GetFileAttributesW(missing file)",
    "GetFileAttributesExW(missing dir)",
    "CreateFileW(missing dir)",
    "FindFirstFileW(missing dir)",
};

struct result { DWORD api, gs, jit; };
static struct result res[MAXT + 1][CHECKS];
static WCHAR missing_file[MAX_PATH], missing_dir_file[MAX_PATH];

/* The same read twice: once from this exe's own code, once from a stub copied
 * into fresh executable memory, as a JIT (Mono's) produces code. CrossOver
 * registers every native PE module with Apple's libd3dshared as a "non-native
 * code region" (CW HACK 22434); generated code is never registered. */
static DWORD gs68( void )
{
    DWORD v;
    __asm__ volatile( "movl %%gs:0x68,%0" : "=r" (v) );
    return v;
}

static DWORD (*jit_gs68)( void );

static void make_jit_stub( void )
{
    static const BYTE code[] = { 0x65, 0x8b, 0x04, 0x25, 0x68, 0x00, 0x00, 0x00,   /* mov eax,gs:[0x68] */
                                 0xc3 };                                        /* ret */
    void *p = VirtualAlloc( NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
    memcpy( p, code, sizeof(code) );
    FlushInstructionCache( GetCurrentProcess(), p, sizeof(code) );
    jit_gs68 = (DWORD (*)(void))p;
}

/* Read gs:0x68 first: GetLastError() does not change the value, but reading
 * the raw slot before any further call keeps them as close as possible. */
static void record( struct result *r )
{
    r->gs = gs68();
    r->jit = jit_gs68();
    r->api = GetLastError();
}

static void run_checks( int slot )
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    WIN32_FIND_DATAW fd;
    HANDLE h;

    SetLastError( 1000 + slot );
    record( &res[slot][0] );

    GetFileAttributesW( missing_file );
    record( &res[slot][1] );

    GetFileAttributesExW( missing_dir_file, GetFileExInfoStandard, &d );
    record( &res[slot][2] );

    h = CreateFileW( missing_dir_file, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL );
    record( &res[slot][3] );
    if (h != INVALID_HANDLE_VALUE) CloseHandle( h );

    h = FindFirstFileW( missing_dir_file, &fd );
    record( &res[slot][4] );
    if (h != INVALID_HANDLE_VALUE) FindClose( h );
}

static DWORD WINAPI worker( void *arg )
{
    run_checks( (int)(INT_PTR)arg );
    return 0;
}

int main( int argc, char **argv )
{
    int nthreads = argc > 1 ? atoi( argv[1] ) : 4;
    int hold_ms = argc > 2 ? atoi( argv[2] ) : 0;
    HANDLE th[MAXT];
    WCHAR tmp[MAX_PATH];
    int t, c, diff = 0, total = 0, diff_set = 0, diff_file = 0, diff_exe = 0, diff_jit = 0;

    if (nthreads < 0) nthreads = 0;
    if (nthreads > MAXT) nthreads = MAXT;

    GetTempPathW( MAX_PATH, tmp );
    swprintf( missing_file, MAX_PATH, L"%lswb-gsprobe-no-such-file.ini", tmp );
    swprintf( missing_dir_file, MAX_PATH, L"%lswb-gsprobe-no-such-dir\\x.ini", tmp );

    make_jit_stub();
    run_checks( 0 );
    for (t = 0; t < nthreads; t++)
        th[t] = CreateThread( NULL, 0, worker, (void *)(INT_PTR)(t + 1), 0, NULL );
    WaitForMultipleObjects( nthreads, th, TRUE, INFINITE );

    printf( "%-8s %-34s %12s %12s %12s\n", "thread", "check", "GetLastError", "gs:0x68 exe", "gs:0x68 jit" );
    for (t = 0; t <= nthreads; t++)
        for (c = 0; c < CHECKS; c++)
        {
            int bad_exe = res[t][c].api != res[t][c].gs, bad_jit = res[t][c].api != res[t][c].jit;
            char who[16];
            snprintf( who, sizeof(who), t ? "worker%d" : "main", t );
            printf( "%-8s %-34s %12lu %12lu %12lu  %s\n", who, names[c], res[t][c].api,
                    res[t][c].gs, res[t][c].jit, bad_exe || bad_jit ? "DIFF" : "ok" );
            total++;
            diff_exe += bad_exe;
            diff_jit += bad_jit;
            if (bad_exe || bad_jit) { diff++; if (c == 0) diff_set++; else diff_file++; }
        }

    fflush( stdout );
    if (hold_ms > 0) Sleep( hold_ms );

    if (!diff)
    {
        printf( "\nRESULT: PASS -- gs:0x68 matched GetLastError() in all %d checks, from the exe and from generated code\n", total );
        return 0;
    }
    printf( "\nRESULT: FAIL -- gs:0x68 differs from GetLastError() in %d of %d checks"
            " (read from the exe: %d, from generated code: %d)\n", diff, total, diff_exe, diff_jit );
    if (!diff_set)
        printf( "hint: only the file-API checks differ -- consistent with ntdll mirroring the last\n"
                "      error but kernelbase.dll lacking wine-0008's inline SetLastError mirror\n" );
    else if (diff_file == total - (nthreads + 1))
        printf( "hint: every check differs -- consistent with no mirror at all (wine-0008 missing)\n" );
    return 1;
}
