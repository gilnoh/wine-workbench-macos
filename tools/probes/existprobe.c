/* Does Wine ever report a file as existing when it does not -- under concurrency?
 *
 *   existprobe.exe <real_root> <missing_root> [threads] [passes] [fwd] [fallback]
 *
 * Walks <real_root> recursively to collect relative paths, then has <threads>
 * threads look each one up under both roots, <passes> times:
 *
 *   <real_root>\<rel>     must exist      (the control: proves "exists" is detectable)
 *   <missing_root>\<rel>  must NOT exist  (its directories do not exist either)
 *
 * using GetFileAttributesW and GetFileAttributesExW -- what a Windows .NET/Mono
 * File.Exists comes down to. Any wrong answer is counted and the first few are
 * printed with GetLastError.
 *
 * With "fwd", the relative part is joined with forward slashes, as .NET's
 * Path.Join(dir, "ammunition/x.ini") produces: <root>\ammunition/x.ini.
 * With "fallback", the .NET File.Exists emulation always takes its
 * FindFirstFileExW branch, to see the error code that branch leaves behind.
 *
 * Written for Sea Power (2026-10-10): its ini preloader runs File.Exists on
 * "StreamingAssets\user\<rel>" from Parallel.ForEach for every ini file; under
 * Wine one such check came back true for a path whose directory does not exist,
 * and the subsequent read failed with "Could not find a part of the path". */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define MAXF 20000

static WCHAR *rel[MAXF];
static int nrel;
static WCHAR real_root[MAX_PATH], miss_root[MAX_PATH];
static int passes = 20;
static int fwd;
static int force_fallback;   /* "fallback": always take the FindFirstFileExW branch */
static volatile LONG false_pos, false_neg, checks, printed;
static volatile LONG gfa_err[64], fff_err[64];   /* GetLastError after each failed call, codes < 64 */
static volatile LONG gfa_other, fff_other, dotnet_true;

static void tally( volatile LONG *h, volatile LONG *other, DWORD e )
{
    if (e < 64) InterlockedIncrement( &h[e] ); else InterlockedIncrement( other );
}

/* What .NET's FileSystem.FillAttributeInfo + FileExists conclude for this path. */
static BOOL dotnet_file_exists( const WCHAR *p )
{
    WIN32_FILE_ATTRIBUTE_DATA d = {0};
    DWORD err = 0;
    if (!GetFileAttributesExW( p, GetFileExInfoStandard, &d ))
    {
        err = GetLastError();
        tally( gfa_err, &gfa_other, err );
        if (force_fallback || err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND && err != ERROR_NOT_READY &&
            err != ERROR_INVALID_NAME && err != ERROR_BAD_PATHNAME && err != ERROR_BAD_NETPATH &&
            err != ERROR_BAD_NET_NAME && err != ERROR_INVALID_PARAMETER && err != ERROR_NETWORK_UNREACHABLE)
        {
            WIN32_FIND_DATAW fd;
            HANDLE h;
            SetLastError( 0xdeadbeef );
            h = FindFirstFileExW( p, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0 );
            if (h == INVALID_HANDLE_VALUE) { err = GetLastError(); tally( fff_err, &fff_other, err ); }
            else { FindClose( h ); err = 0; d.dwFileAttributes = fd.dwFileAttributes; }
        }
    }
    return err == 0 && d.dwFileAttributes != INVALID_FILE_ATTRIBUTES && !(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void walk( const WCHAR *dir, const WCHAR *prefix )
{
    WCHAR pat[MAX_PATH], sub[MAX_PATH], subrel[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    swprintf( pat, MAX_PATH, L"%ls\\*", dir );
    if ((h = FindFirstFileW( pat, &fd )) == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!wcscmp( fd.cFileName, L"." ) || !wcscmp( fd.cFileName, L".." )) continue;
        swprintf( sub, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName );
        swprintf( subrel, MAX_PATH, prefix[0] ? L"%ls\\%ls" : L"%ls%ls", prefix, fd.cFileName );
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk( sub, subrel );
        else if (nrel < MAXF)
        {
            WCHAR *r = _wcsdup( subrel );
            if (fwd) for (WCHAR *c = r; *c; c++) if (*c == '\\') *c = '/';
            rel[nrel++] = r;
        }
    } while (FindNextFileW( h, &fd ));
    FindClose( h );
}

static void report( const char *what, const WCHAR *path, DWORD err )
{
    if (InterlockedIncrement( &printed ) <= 10)
        printf( "  %s: %ls (GetLastError=%lu)\n", what, path, err );
}

static DWORD WINAPI worker( void *arg )
{
    int start = (int)(INT_PTR)arg;
    WCHAR p[MAX_PATH * 2];
    WIN32_FILE_ATTRIBUTE_DATA ad;

    for (int pass = 0; pass < passes; pass++)
        for (int k = 0; k < nrel; k++)
        {
            int i = (k + start * 97) % nrel;   /* threads walk from different offsets */
            DWORD a; BOOL ok;

            swprintf( p, ARRAYSIZE(p), L"%ls\\%ls", miss_root, rel[i] );
            SetLastError( 0 );
            a = GetFileAttributesW( p );
            if (a != INVALID_FILE_ATTRIBUTES) { InterlockedIncrement( &false_pos ); report( "EXISTS? (GetFileAttributesW)", p, GetLastError() ); }
            SetLastError( 0 );
            ok = GetFileAttributesExW( p, GetFileExInfoStandard, &ad );
            if (ok) { InterlockedIncrement( &false_pos ); report( "EXISTS? (GetFileAttributesExW)", p, GetLastError() ); }

            swprintf( p, ARRAYSIZE(p), L"%ls\\%ls", real_root, rel[i] );
            SetLastError( 0 );
            ok = GetFileAttributesExW( p, GetFileExInfoStandard, &ad );
            if (!ok) { InterlockedIncrement( &false_neg ); report( "MISSING? (GetFileAttributesExW)", p, GetLastError() ); }

            swprintf( p, ARRAYSIZE(p), L"%ls\\%ls", miss_root, rel[i] );
            if (dotnet_file_exists( p ) && !wcschr( rel[i], '\\' ) && !wcschr( rel[i], '/' )) ;  /* top-level files may really exist */
            else if (dotnet_file_exists( p )) { InterlockedIncrement( &dotnet_true ); report( "EXISTS? (.NET File.Exists logic)", p, 0 ); }

            InterlockedExchangeAdd( &checks, 3 );
        }
    return 0;
}

int main( int argc, char **argv )
{
    int nthreads = 16;
    HANDLE th[256];
    DWORD t0;

    if (argc < 3) { printf( "usage: existprobe.exe <real_root> <missing_root> [threads] [passes]\n" ); return 2; }
    MultiByteToWideChar( CP_ACP, 0, argv[1], -1, real_root, MAX_PATH );
    MultiByteToWideChar( CP_ACP, 0, argv[2], -1, miss_root, MAX_PATH );
    if (argc > 3) nthreads = atoi( argv[3] );
    if (argc > 4) passes = atoi( argv[4] );
    for (int a = 5; a < argc; a++)
    {
        if (!strcmp( argv[a], "fwd" )) fwd = 1;
        if (!strcmp( argv[a], "fallback" )) force_fallback = 1;
    }
    if (nthreads < 1 || nthreads > 256) nthreads = 16;

    walk( real_root, L"" );
    printf( "files under real root: %d; threads: %d; passes: %d; separators: %s\n", nrel, nthreads, passes, fwd ? "mixed (\\ then /)" : "backslash" );
    if (!nrel) return 2;

    t0 = GetTickCount();
    for (int i = 0; i < nthreads; i++) th[i] = CreateThread( NULL, 0, worker, (void *)(INT_PTR)i, 0, NULL );
    WaitForMultipleObjects( nthreads, th, TRUE, INFINITE );
    printf( "checks: %ld in %lu ms\n", checks, GetTickCount() - t0 );
    printf( "false 'exists' under missing root: %ld\n", false_pos );
    printf( "false 'missing' under real root:   %ld\n", false_neg );
    printf( ".NET File.Exists logic says 'exists' (missing root, below top level): %ld\n", dotnet_true );
    printf( "GetLastError after failed GetFileAttributesExW (missing root):" );
    for (int e = 0; e < 64; e++) if (gfa_err[e]) printf( " %d:%ld", e, gfa_err[e] );
    printf( " other:%ld\n", gfa_other );
    printf( "GetLastError after failed FindFirstFileExW fallback:" );
    for (int e = 0; e < 64; e++) if (fff_err[e]) printf( " %d:%ld", e, fff_err[e] );
    printf( " other:%ld\n", fff_other );
    return (false_pos || false_neg || dotnet_true) ? 1 : 0;
}
