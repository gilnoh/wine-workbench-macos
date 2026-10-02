/* Stress msync's wait registration with the classic worker pattern:
 * WaitForMultipleObjects({quit_event, work_event}) where quit never fires.
 *
 *   waitprobe.exe [iterations]
 *
 * Two threads ping-pong through auto-reset events. Run with WINEMSYNC=1
 * WINEMSYNC_STATS=1 on a runtime carrying wine-0006: wineserver then prints
 * msync-stat lines, and a steadily rising in_use that does not fall back after
 * the run is the node-pool leak behind "msync: warn: node memory pool exhausted". */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HANDLE quit_ev, ping, pong;
static LONG iters;

static DWORD WINAPI worker( void *arg )
{
    HANDLE w[2] = { quit_ev, ping };
    for (;;)
    {
        DWORD r = WaitForMultipleObjects( 2, w, FALSE, INFINITE );
        if (r == WAIT_OBJECT_0) return 0;
        SetEvent( pong );
    }
}

int main( int argc, char **argv )
{
    LARGE_INTEGER f, t0, t1;
    HANDLE th, w[2];
    LONG i, n = argc > 1 ? atol( argv[1] ) : 200000;

    quit_ev = CreateEventA( NULL, TRUE, FALSE, NULL );
    ping = CreateEventA( NULL, FALSE, FALSE, NULL );
    pong = CreateEventA( NULL, FALSE, FALSE, NULL );
    th = CreateThread( NULL, 0, worker, NULL, 0, NULL );
    w[0] = quit_ev; w[1] = pong;

    QueryPerformanceFrequency( &f );
    QueryPerformanceCounter( &t0 );
    for (i = 0; i < n; i++)
    {
        SetEvent( ping );
        WaitForMultipleObjects( 2, w, FALSE, INFINITE );
    }
    QueryPerformanceCounter( &t1 );
    SetEvent( quit_ev );
    WaitForSingleObject( th, INFINITE );
    iters = n;
    printf( "%ld round trips in %.2f s (%.1f us each)\n", (long)iters,
            (t1.QuadPart - t0.QuadPart) / (double)f.QuadPart,
            (t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart / n );
    return 0;
}
