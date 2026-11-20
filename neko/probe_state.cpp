// probe_state.cpp — inspect an ALREADY RUNNING Neko98 process.
// Purpose: when the process exists but the cat is not visible on the desktop,
// decide which layer is at fault:
//   * window missing / destroyed        -> pet object never created
//   * window exists but IsWindowVisible=FALSE -> ShowWindow never called / hidden
//   * window visible but zero-size / off-screen -> geometry bug
//   * window visible, size ok, but layered alpha all zero -> compositing bug
//   * window visible and painted, but behind other windows / on another virtual desktop
// Build: g++ -O2 -o probe_state.exe probe_state.cpp -lgdi32 -luser32
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD g_pid = 0;
static int   g_found = 0;

// Read the layered window's actual pixels back through GetDC(NULL)+BitBlt and
// report how many pixels are non-transparent.  A layered window returns what is
// on screen, so this answers "is anything actually painted".
static void ProbePixels( HWND hWnd, const RECT* prc )
{
    int w = prc->right - prc->left;
    int h = prc->bottom - prc->top;
    if( w <= 0 || h <= 0 || w > 4000 || h > 4000 )
    {
        printf( "    pixel probe skipped (bad size %dx%d)\n", w, h );
        return;
    }

    HDC hScreen = GetDC( NULL );
    if( !hScreen ) { printf( "    pixel probe skipped (no screen DC)\n" ); return; }

    HDC     hMem  = CreateCompatibleDC( hScreen );
    HBITMAP hBmp  = CreateCompatibleBitmap( hScreen, w, h );
    HGDIOBJ hOld  = SelectObject( hMem, hBmp );

    BOOL fOk = BitBlt( hMem, 0, 0, w, h, hScreen, prc->left, prc->top, SRCCOPY );

    BITMAPINFO bi;
    memset( &bi, 0, sizeof bi );
    bi.bmiHeader.biSize        = sizeof( BITMAPINFOHEADER );
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;              // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    BYTE* p = (BYTE*) malloc( (size_t)w * h * 4 );
    int   nGot = 0;
    if( p && fOk )
        nGot = GetDIBits( hMem, hBmp, 0, h, p, &bi, DIB_RGB_COLORS );

    if( nGot == h && p )
    {
        // The desktop behind the cat is not uniform, so "how much of this rect
        // differs from its immediate surroundings" is the useful measure.  Use
        // the top-left pixel as the background reference and count deviations.
        BYTE refB = p[0], refG = p[1], refR = p[2];
        int  nDiff = 0;
        for( int i = 0; i < w * h; i++ )
        {
            int d = abs( (int)p[i*4+0] - refB )
                  + abs( (int)p[i*4+1] - refG )
                  + abs( (int)p[i*4+2] - refR );
            if( d > 24 ) nDiff++;
        }
        printf( "    pixels: %d of %d differ from the corner reference (%.1f%%)\n",
                nDiff, w * h, 100.0 * nDiff / ( w * h ) );
        if( nDiff == 0 )
            printf( "    -> the rect is a FLAT colour: nothing was painted here\n" );
        else if( nDiff * 100 / ( w * h ) > 90 )
            printf( "    -> nearly the whole rect differs: looks like an opaque box\n" );
        else
            printf( "    -> partial coverage: consistent with a transparent sprite\n" );
    }
    else
    {
        printf( "    pixel probe failed (BitBlt=%d GetDIBits=%d/%d)\n", fOk, nGot, h );
    }

    if( p ) free( p );
    SelectObject( hMem, hOld );
    DeleteObject( hBmp );
    DeleteDC( hMem );
    ReleaseDC( NULL, hScreen );
}

static BOOL CALLBACK EnumProc( HWND hWnd, LPARAM )
{
    DWORD dwPid = 0;
    GetWindowThreadProcessId( hWnd, &dwPid );
    if( dwPid != g_pid )
        return TRUE;

    char szClass[256] = {0}, szText[256] = {0};
    GetClassNameA( hWnd, szClass, sizeof szClass - 1 );
    GetWindowTextA( hWnd, szText, sizeof szText - 1 );

    RECT rc = {0,0,0,0};
    GetWindowRect( hWnd, &rc );

    LONG_PTR ex = GetWindowLongPtrA( hWnd, GWL_EXSTYLE );
    LONG_PTR st = GetWindowLongPtrA( hWnd, GWL_STYLE );

    // How many monitors does this rect actually touch?
    int  nMon = 0;
    {
        HMONITOR seen[8]; int nSeen = 0;
        const POINT pts[5] = {
            { rc.left, rc.top }, { rc.right-1, rc.top },
            { rc.left, rc.bottom-1 }, { rc.right-1, rc.bottom-1 },
            { (rc.left+rc.right)/2, (rc.top+rc.bottom)/2 }
        };
        for( int i = 0; i < 5; i++ )
        {
            HMONITOR hm = MonitorFromPoint( pts[i], MONITOR_DEFAULTTONEAREST );
            BOOL dup = FALSE;
            for( int j = 0; j < nSeen; j++ ) if( seen[j] == hm ) dup = TRUE;
            if( !dup && nSeen < 8 ) seen[nSeen++] = hm;
        }
        nMon = nSeen;
    }

    printf( "  hwnd=%p  class=%-28s text=\"%s\"\n", (void*)hWnd, szClass, szText );
    printf( "    rect   = %ld,%ld  %ldx%ld\n", rc.left, rc.top,
            rc.right - rc.left, rc.bottom - rc.top );
    printf( "    visible= %d   iconic=%d   enabled=%d   monitors=%d\n",
            IsWindowVisible( hWnd ), IsIconic( hWnd ), IsWindowEnabled( hWnd ), nMon );
    printf( "    style  = 0x%08lX   exstyle= 0x%08lX  (LAYERED=%d TOPMOST=%d TRANSPARENT=%d TOOLWIN=%d NOACTIVATE=%d)\n",
            (unsigned long)st, (unsigned long)ex,
            ( ex & WS_EX_LAYERED )     ? 1 : 0,
            ( ex & WS_EX_TOPMOST )     ? 1 : 0,
            ( ex & WS_EX_TRANSPARENT ) ? 1 : 0,
            ( ex & WS_EX_TOOLWINDOW )  ? 1 : 0,
            ( ex & WS_EX_NOACTIVATE )  ? 1 : 0 );

    // A layered window reports its alpha in LWA_ALPHA only if it used
    // SetLayeredWindowAttributes.  UpdateLayeredWindow based windows report 0
    // here and are still perfectly visible, so this is informational only.
    if( ex & WS_EX_LAYERED )
    {
        BYTE  alpha = 0;
        DWORD flags = 0;
        if( GetLayeredWindowAttributes( hWnd, NULL, &alpha, &flags ) )
            printf( "    LWA: alpha=%u flags=0x%08lX (0 = per-pixel alpha via UpdateLayeredWindow)\n",
                    alpha, (unsigned long)flags );
        else
            printf( "    LWA: GetLayeredWindowAttributes failed (err=%lu)\n", GetLastError() );
    }

    // Is it covered?  WindowFromPoint at the centre tells us who owns the pixel.
    {
        POINT c = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
        HWND hTop = WindowFromPoint( c );
        DWORD dwTopPid = 0;
        if( hTop ) GetWindowThreadProcessId( hTop, &dwTopPid );
        char szTopClass[128] = {0};
        if( hTop ) GetClassNameA( hTop, szTopClass, sizeof szTopClass - 1 );
        printf( "    at centre (%ld,%ld): topmost window is %p class=%s pid=%lu %s\n",
                c.x, c.y, (void*)hTop, szTopClass, (unsigned long)dwTopPid,
                ( hTop == hWnd ) ? "<-- OURS: not covered" :
                ( dwTopPid == g_pid ) ? "<-- ours, different hwnd" : "<-- COVERED BY ANOTHER PROCESS" );
    }

    // Which desktop / virtual desktop is this window on?  Compare with ours.
    printf( "    desktop: ours=%p theirs=? (a window on another virtual desktop still enumerates)\n",
            (void*)GetThreadDesktop( GetCurrentThreadId() ) );

    if( ( ex & WS_EX_LAYERED ) && IsWindowVisible( hWnd ) )
        ProbePixels( hWnd, &rc );

    printf( "\n" );
    g_found++;
    return TRUE;
}

int main( int argc, char** argv )
{
    if( argc < 2 )
    {
        printf( "usage: probe_state.exe <pid>\n" );
        return 2;
    }
    g_pid = (DWORD) atol( argv[1] );
    if( g_pid == 0 )
    {
        printf( "bad pid\n" );
        return 2;
    }

    // Match the pet's own awareness so the coordinates we print are the SAME
    // physical pixels the pet uses (the binary is PerMonitorV2 aware).
    typedef BOOL (WINAPI *PFN_SET)(HANDLE);
    HMODULE hUser = LoadLibraryA( "user32.dll" );
    if( hUser )
    {
        PFN_SET pSet = (PFN_SET) GetProcAddress( hUser, "SetProcessDpiAwarenessContext" );
        if( pSet ) pSet( (HANDLE)(LONG_PTR)-4 );   // PER_MONITOR_AWARE_V2
    }

    printf( "=== probing pid %lu ===\n", (unsigned long)g_pid );
    printf( "awareness: PerMonitorV2 requested\n" );
    printf( "virtual screen: %d,%d %dx%d\n",
            GetSystemMetrics( SM_XVIRTUALSCREEN ),  GetSystemMetrics( SM_YVIRTUALSCREEN ),
            GetSystemMetrics( SM_CXVIRTUALSCREEN ), GetSystemMetrics( SM_CYVIRTUALSCREEN ) );
    printf( "primary screen: %dx%d\n\n",
            GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ) );

    EnumWindows( EnumProc, 0 );

    if( g_found == 0 )
        printf( "RESULT: the process owns NO top-level window at all -> the window was never created or was destroyed.\n" );
    else
        printf( "RESULT: %d top-level window(s) enumerated (see above).\n", g_found );

    return 0;
}
