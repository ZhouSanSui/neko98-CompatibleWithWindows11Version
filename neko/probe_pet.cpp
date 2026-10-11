// probe_pet.cpp - launch-and-observe probe for the real Neko98.exe.
//
// The render unit test proves the compositing maths; this probe proves the
// SHIPPED BINARY actually runs its AI loop, keeps its layered window on screen,
// and moves it around without dying. It observes the pet from the outside (no
// production code linked), which is what a user sees.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static DWORD g_enumPid = 0;
static HWND  g_enumHwnd = NULL;
static RECT  g_enumRect = {0,0,0,0};
static LONG  g_enumEx = 0;

static BOOL CALLBACK FindPetWindow( HWND hWnd, LPARAM lp )
{
    DWORD pid = 0;
    GetWindowThreadProcessId( hWnd, &pid );
    if( pid != g_enumPid ) return TRUE;
    if( !IsWindowVisible( hWnd ) ) return TRUE;

    RECT rc;
    GetWindowRect( hWnd, &rc );
    if( ( rc.right - rc.left ) <= 0 || ( rc.bottom - rc.top ) <= 0 ) return TRUE;

    g_enumHwnd = hWnd;
    g_enumRect = rc;
    g_enumEx   = GetWindowLong( hWnd, GWL_EXSTYLE );
    return FALSE;
}

//diagnostic: list every top-level window the process owns
static DWORD g_dumpPid = 0;
static BOOL CALLBACK DumpOne( HWND h, LPARAM lp )
{
    DWORD p = 0;
    GetWindowThreadProcessId( h, &p );
    if( p != g_dumpPid ) return TRUE;

    RECT r;
    GetWindowRect( h, &r );
    char cls[128] = {0};
    GetClassNameA( h, cls, 127 );
    printf( "    hwnd=0x%p vis=%d class=%-24s rect=%ld,%ld %ldx%ld ex=0x%08lX\n",
            h, IsWindowVisible(h) ? 1 : 0, cls,
            r.left, r.top, r.right-r.left, r.bottom-r.top,
            (unsigned long)GetWindowLong(h, GWL_EXSTYLE) );
    return TRUE;
}

static void DumpWindows( DWORD pid )
{
    printf( "  windows of pid=%lu:\n", pid );
    g_dumpPid = pid;
    EnumWindows( DumpOne, 0 );
}

int main( int argc, char** argv )
{
    const char* exe = ( argc > 1 ) ? argv[1] : "Neko98.exe";
    const int nSeconds = ( argc > 2 ) ? atoi(argv[2]) : 12;

    printf( "=== Neko98 live probe: %s for %d s ===\n", exe, nSeconds );

    STARTUPINFOA si = {0}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    char cmd[512];
    sprintf( cmd, "\"%s\"", exe );

    if( !CreateProcessA( NULL, cmd, NULL, NULL, FALSE,
                         DETACHED_PROCESS, NULL, NULL, &si, &pi ) )
    {
        printf( "FAIL: CreateProcess error=%lu\n", GetLastError() );
        return 1;
    }
    printf( "started pid=%lu\n", pi.dwProcessId );

    int nFail = 0;
    int nSamples = 0, nMoved = 0, nLayeredSeen = 0;
    RECT first = {0,0,0,0}, last = {0,0,0,0};
    BOOL fHaveFirst = FALSE;

    for( int t = 0; t < nSeconds * 2; t++ )
    {
        Sleep( 500 );

        DWORD code = 0;
        if( !GetExitCodeProcess( pi.hProcess, &code ) || code != STILL_ACTIVE )
        {
            printf( "\nFAIL: the pet EXITED early with code %lu after ~%.1f s\n",
                    code, t * 0.5 );
            nFail++;
            break;
        }

        g_enumPid = pi.dwProcessId;
        g_enumHwnd = NULL;
        EnumWindows( FindPetWindow, 0 );
        if( !g_enumHwnd ) continue;

        nSamples++;
        if( g_enumEx & WS_EX_LAYERED ) nLayeredSeen++;

        if( !fHaveFirst ) { first = g_enumRect; fHaveFirst = TRUE; }
        else if( g_enumRect.left != last.left || g_enumRect.top != last.top ) nMoved++;
        last = g_enumRect;
    }

    printf( "\nsamples=%d  layered-window-seen=%d  position-changes=%d\n",
            nSamples, nLayeredSeen, nMoved );
    printf( "first rect=%ld,%ld %ldx%ld   last rect=%ld,%ld %ldx%ld\n",
            first.left, first.top, first.right-first.left, first.bottom-first.top,
            last.left, last.top, last.right-last.left, last.bottom-last.top );

    DumpWindows( pi.dwProcessId );

    if( nSamples < 3 )            { printf( "FAIL: pet window rarely visible\n" ); nFail++; }
    if( nLayeredSeen < nSamples ) { printf( "FAIL: window not WS_EX_LAYERED\n" ); nFail++; }
    if( nMoved == 0 )             { printf( "WARN: pet never moved (idle/sleeping is legal)\n" ); }

    DWORD code = 0;
    GetExitCodeProcess( pi.hProcess, &code );
    if( code != STILL_ACTIVE ) { printf( "FAIL: exited with %lu\n", code ); nFail++; }

    TerminateProcess( pi.hProcess, 0 );
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );
    printf( "probe terminated the pet\n" );

    printf( "\n=== %s ===\n", nFail ? "PROBE FAILED" : "PROBE PASSED" );
    return nFail ? 2 : 0;
}
