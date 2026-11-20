// probe_watch.cpp — watch the pet over time and classify its state.
//
// Distinguishes the two explanations for "process runs but the cat is not
// properly on screen":
//   (a) the pet is simply asleep/idle  -> rect stable AND content stable, but
//       the cat is drawn (non-flat pixels)
//   (b) the renderer is FROZEN        -> rect stable AND content stable for the
//       WHOLE observation, with no state change at all
//   (c) the window is buried/off-screen -> rect outside every monitor
//
// Also reports whether the window rect ever leaves the virtual screen, which is
// the "invisible because off-screen" failure mode.
//
// Usage: probe_watch.exe <pid> [seconds]
// Build: g++ -O2 -o probe_watch.exe probe_watch.cpp -lgdi32 -luser32
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD g_pid    = 0;
static HWND  g_target = NULL;

static BOOL CALLBACK FindProc( HWND hWnd, LPARAM )
{
    if( g_target ) return FALSE;
    DWORD dwPid = 0;
    GetWindowThreadProcessId( hWnd, &dwPid );
    if( dwPid != g_pid ) return TRUE;
    char szClass[256] = {0};
    GetClassNameA( hWnd, szClass, sizeof szClass - 1 );
    if( strstr( szClass, "TransparentPet" ) == NULL ) return TRUE;
    g_target = hWnd;
    return FALSE;
}

// A cheap fingerprint of what is on screen inside a rect: sum of pixel values.
static long long Fingerprint( const RECT* prc )
{
    int w = prc->right - prc->left;
    int h = prc->bottom - prc->top;
    if( w <= 0 || h <= 0 ) return 0;

    HDC hScreen = GetDC( NULL );
    if( !hScreen ) return 0;
    HDC hMem = CreateCompatibleDC( hScreen );
    HBITMAP hBmp = CreateCompatibleBitmap( hScreen, w, h );
    HGDIOBJ hOld = SelectObject( hMem, hBmp );
    BitBlt( hMem, 0, 0, w, h, hScreen, prc->left, prc->top, SRCCOPY );

    BITMAPINFO bi;
    memset( &bi, 0, sizeof bi );
    bi.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    long long sum = 0;
    BYTE* p = (BYTE*) malloc( (size_t)w * h * 4 );
    if( p && GetDIBits( hMem, hBmp, 0, h, p, &bi, DIB_RGB_COLORS ) == h )
    {
        for( int i = 0; i < w * h; i++ )
            sum += p[i*4+0] + p[i*4+1] + p[i*4+2];
    }
    if( p ) free( p );

    SelectObject( hMem, hOld );
    DeleteObject( hBmp );
    DeleteDC( hMem );
    ReleaseDC( NULL, hScreen );
    return sum;
}

int main( int argc, char** argv )
{
    if( argc < 2 ) { printf( "usage: probe_watch.exe <pid> [seconds]\n" ); return 2; }
    g_pid = (DWORD) atol( argv[1] );
    int nSecs = ( argc >= 3 ) ? atoi( argv[2] ) : 10;
    if( nSecs < 2 ) nSecs = 2;

    typedef BOOL (WINAPI *PFN_SET)(HANDLE);
    HMODULE hUser = LoadLibraryA( "user32.dll" );
    if( hUser )
    {
        PFN_SET pSet = (PFN_SET) GetProcAddress( hUser, "SetProcessDpiAwarenessContext" );
        if( pSet ) pSet( (HANDLE)(LONG_PTR)-4 );
    }

    EnumWindows( FindProc, 0 );
    if( !g_target ) { printf( "no pet window for pid %lu\n", (unsigned long)g_pid ); return 1; }

    int vx = GetSystemMetrics( SM_XVIRTUALSCREEN );
    int vy = GetSystemMetrics( SM_YVIRTUALSCREEN );
    int vw = GetSystemMetrics( SM_CXVIRTUALSCREEN );
    int vh = GetSystemMetrics( SM_CYVIRTUALSCREEN );
    printf( "virtual screen: %d,%d %dx%d\n", vx, vy, vw, vh );
    printf( "watching for %d s (500 ms samples)\n\n", nSecs );

    int  nSamples = 0, nRectChanges = 0, nFpChanges = 0, nOffScreen = 0;
    RECT rcPrev = {0,0,0,0};
    long long fpPrev = 0;
    int  nFlat = 0;

    int nIter = nSecs * 2;
    for( int i = 0; i < nIter; i++ )
    {
        if( !IsWindow( g_target ) )
        {
            printf( "  [%4d ms] window DESTROYED\n", i * 500 );
            break;
        }
        RECT rc = {0,0,0,0};
        GetWindowRect( g_target, &rc );
        long long fp = Fingerprint( &rc );

        BOOL fVisible = IsWindowVisible( g_target );
        BOOL fOff = ( rc.right <= vx || rc.left >= vx + vw ||
                      rc.bottom <= vy || rc.top >= vy + vh );
        if( fOff ) nOffScreen++;

        if( i > 0 )
        {
            if( rc.left != rcPrev.left || rc.top != rcPrev.top ||
                rc.right != rcPrev.right || rc.bottom != rcPrev.bottom ) nRectChanges++;
            if( fp != fpPrev ) nFpChanges++;
        }

        // "flat" = the rect is a constant colour (background only)
        BOOL fFlat = ( fp == 0 );
        if( fFlat ) nFlat++;

        if( i % 2 == 0 || i >= nIter - 2 )
        {
            printf( "  [%4d ms] vis=%d rect=%ld,%ld %ldx%ld fp=%lld%s%s\n",
                    i * 500, fVisible,
                    rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                    fp,
                    fOff ? "  <-- OFF-SCREEN" : "",
                    fVisible ? "" : "  <-- NOT VISIBLE" );
        }

        rcPrev = rc;
        fpPrev = fp;
        nSamples++;
        Sleep( 500 );
    }

    printf( "\n--- summary over %d samples ---\n", nSamples );
    printf( "  rect changed   : %d times\n", nRectChanges );
    printf( "  content changed: %d times\n", nFpChanges );
    printf( "  off-screen     : %d samples\n", nOffScreen );
    printf( "  flat (empty)   : %d samples\n", nFlat );

    printf( "\nVERDICT: " );
    if( nOffScreen > 0 )
        printf( "OFF-SCREEN for part of the run -> the cat is parked outside the desktop.\n" );
    else if( nFlat == nSamples )
        printf( "EMPTY -> the window paints nothing at all for the whole run.\n" );
    else if( nRectChanges == 0 && nFpChanges == 0 )
        printf( "FROZEN -> drawn, but neither the position nor the pixels ever change.\n" );
    else
        printf( "ALIVE -> the pet both moves and repaints.\n" );

    return 0;
}
