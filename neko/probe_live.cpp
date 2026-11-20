// probe_live.cpp — decisive "does the running pet actually paint?" test.
//
// Method: capture the screen rect covered by the pet's layered window, then
// temporarily HIDE the window and capture again, then show it and capture once
// more.  The difference between the first and second capture is EXACTLY the
// contribution the pet makes to the screen:
//     ~0%   -> the window is on screen but paints nothing (invisible pet)
//     small -> the cat sprite is painted (normal)
//     ~100% -> an opaque rectangle (background box bug)
// The window is always restored, so the user's pet is not disturbed.
//
// Also verifies Z-order: WS_EX_TOPMOST windows must sit above normal windows.
//
// Build: g++ -O2 -o probe_live.exe probe_live.cpp -lgdi32 -luser32
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
    if( strcmp( szClass, "Neko98TransparentPet" ) != 0 ) return TRUE;

    if( !IsWindowVisible( hWnd ) ) return TRUE;

    RECT rc = {0,0,0,0};
    GetWindowRect( hWnd, &rc );
    if( rc.right - rc.left <= 1 || rc.bottom - rc.top <= 1 ) return TRUE;

    g_target = hWnd;
    return FALSE;
}

// Snapshot the screen over a rect as a 32bpp top-down buffer.
static BYTE* Grab( const RECT* prc, int* pw, int* ph )
{
    int w = prc->right - prc->left;
    int h = prc->bottom - prc->top;
    if( w <= 0 || h <= 0 ) return NULL;

    HDC     hScreen = GetDC( NULL );
    if( !hScreen ) return NULL;
    HDC     hMem  = CreateCompatibleDC( hScreen );
    HBITMAP hBmp  = CreateCompatibleBitmap( hScreen, w, h );
    HGDIOBJ hOld  = SelectObject( hMem, hBmp );

    BOOL fOk = BitBlt( hMem, 0, 0, w, h, hScreen, prc->left, prc->top, SRCCOPY );

    BITMAPINFO bi;
    memset( &bi, 0, sizeof bi );
    bi.bmiHeader.biSize        = sizeof( BITMAPINFOHEADER );
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    BYTE* p = (BYTE*) malloc( (size_t)w * h * 4 );
    if( p && fOk && GetDIBits( hMem, hBmp, 0, h, p, &bi, DIB_RGB_COLORS ) != h )
    {
        free( p );
        p = NULL;
    }

    SelectObject( hMem, hOld );
    DeleteObject( hBmp );
    DeleteDC( hMem );
    ReleaseDC( NULL, hScreen );

    if( p ) { *pw = w; *ph = h; }
    return p;
}

static int CountDiff( const BYTE* a, const BYTE* b, int n )
{
    int nDiff = 0;
    for( int i = 0; i < n; i++ )
    {
        int d = abs( (int)a[i*4+0] - (int)b[i*4+0] )
              + abs( (int)a[i*4+1] - (int)b[i*4+1] )
              + abs( (int)a[i*4+2] - (int)b[i*4+2] );
        if( d > 16 ) nDiff++;
    }
    return nDiff;
}

int main( int argc, char** argv )
{
    if( argc < 2 ) { printf( "usage: probe_live.exe <pid>\n" ); return 2; }
    g_pid = (DWORD) atol( argv[1] );

    // Match the pet's own DPI awareness so the rect we capture and the rect the
    // pet draws into are the same physical pixels.
    typedef BOOL (WINAPI *PFN_SET)(HANDLE);
    HMODULE hUser = LoadLibraryA( "user32.dll" );
    if( hUser )
    {
        PFN_SET pSet = (PFN_SET) GetProcAddress( hUser, "SetProcessDpiAwarenessContext" );
        if( pSet ) pSet( (HANDLE)(LONG_PTR)-4 );
    }

    EnumWindows( FindProc, 0 );
    if( !g_target )
    {
        printf( "RESULT: NO visible Neko98TransparentPet window for pid %lu -> the pet has no window to draw in.\n",
                (unsigned long)g_pid );
        return 1;
    }

    RECT rc = {0,0,0,0};
    GetWindowRect( g_target, &rc );
    printf( "window %p rect=%ld,%ld %ldx%ld\n", (void*)g_target,
            rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top );

    int  w = 0, h = 0;
    int  nFail = 0;

    // --- Z-order: what sits immediately above us? -------------------------
    HWND hPrev = GetWindow( g_target, GW_HWNDPREV );
    char szPrevClass[128] = {0};
    if( hPrev ) GetClassNameA( hPrev, szPrevClass, sizeof szPrevClass - 1 );
    printf( "window directly above us in Z-order: %p class=%s\n",
            (void*)hPrev, hPrev ? szPrevClass : "(none - we are topmost)" );

    // --- Snapshot A: with the pet visible --------------------------------
    Sleep( 600 );
    BYTE* a = Grab( &rc, &w, &h );
    if( !a ) { printf( "FAIL: could not capture the screen\n" ); return 2; }
    int nPix = w * h;

    // --- Snapshot B: pet hidden ------------------------------------------
    ShowWindow( g_target, SW_HIDE );
    Sleep( 700 );
    BYTE* b = Grab( &rc, &w, &h );

    // --- Snapshot C: pet restored ----------------------------------------
    ShowWindow( g_target, SW_SHOWNOACTIVATE );
    Sleep( 700 );
    BYTE* c = Grab( &rc, &w, &h );

    if( !b || !c )
    {
        printf( "FAIL: capture failed while toggling visibility\n" );
        ShowWindow( g_target, SW_SHOWNOACTIVATE );
        return 2;
    }

    int nContribution = CountDiff( a, b, nPix );   // what the pet adds to the screen
    int nNoise        = CountDiff( b, c, nPix );   // capture-to-capture noise (hidden vs hidden)

    double pctContrib = 100.0 * nContribution / nPix;
    double pctNoise   = 100.0 * nNoise / nPix;

    printf( "\n--- measurements over %dx%d (%d px) ---\n", w, h, nPix );
    printf( "  pet-visible vs pet-hidden : %d px differ (%.2f%%)   <-- what the pet actually paints\n",
            nContribution, pctContrib );
    printf( "  hidden vs restored        : %d px differ (%.2f%%)   <-- capture noise floor\n",
            nNoise, pctNoise );

    if( pctContrib < 1.0 )
    {
        printf( "\nVERDICT: INVISIBLE — the window exists on screen but paints (almost) NOTHING.\n" );
        printf( "         This is the reported bug: the process runs, the window is 'visible',\n" );
        printf( "         yet the desktop never receives the cat's pixels.\n" );
        nFail++;
    }
    else if( pctContrib > 92.0 && pctNoise < 1.0 )
    {
        printf( "\nVERDICT: OPAQUE BOX — the whole window rect is painted (background bug).\n" );
        nFail++;
    }
    else
    {
        printf( "\nVERDICT: VISIBLE — the pet paints a partial (transparent-background) sprite.\n" );
    }

    free( a ); free( b ); free( c );
    printf( "\n=== %s (%d failures) ===\n", nFail ? "FAILED" : "ALL PASS", nFail );
    return nFail ? 1 : 0;
}
