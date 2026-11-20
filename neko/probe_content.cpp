// probe_content.cpp — read the pet window's OWN bitmap, not the screen.
//
// Why this exists: sleeping/shifting capture noise makes "screen before vs after
// hiding" ambiguous (both comparisons showed exactly 140 px).  Reading the
// window's own DC removes the desktop from the equation entirely:
//   * GetDC(hwnd)+BitBlt  -> the layered window's backing bitmap
//   * PrintWindow(PW_RENDERFULLCONTENT) -> the composed result
// Both are reported with a non-black / non-transparent pixel count.  A window
// whose content is empty (all alpha 0 or all black) proves the sprite pipeline
// produced nothing, regardless of what the desktop shows.
//
// Build: g++ -O2 -o probe_content.exe probe_content.cpp -lgdi32 -luser32
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
    g_target = hWnd;
    return FALSE;
}

// Analyse an HBITMAP: count opaque pixels (alpha>0) and non-black pixels.
static void Analyse( HBITMAP hBmp, int w, int h, const char* szLabel )
{
    BITMAPINFO bi;
    memset( &bi, 0, sizeof bi );
    bi.bmiHeader.biSize        = sizeof( BITMAPINFOHEADER );
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    BYTE* p = (BYTE*) malloc( (size_t)w * h * 4 );
    if( !p ) return;

    HDC hMem = CreateCompatibleDC( NULL );
    HGDIOBJ hOld = SelectObject( hMem, hBmp );
    int nGot = GetDIBits( hMem, hBmp, 0, h, p, &bi, DIB_RGB_COLORS );
    SelectObject( hMem, hOld );
    DeleteDC( hMem );

    if( nGot != h )
    {
        printf( "  %-28s : GetDIBits failed (%d/%d)\n", szLabel, nGot, h );
        free( p );
        return;
    }

    int nOpaque = 0, nNonBlack = 0, nAlpha255 = 0;
    int minX = w, minY = h, maxX = -1, maxY = -1;
    for( int y = 0; y < h; y++ )
    {
        for( int x = 0; x < w; x++ )
        {
            BYTE* q = p + ( (size_t)y * w + x ) * 4;
            if( q[3] > 0 )
            {
                nOpaque++;
                if( q[3] >= 250 ) nAlpha255++;
                if( x < minX ) minX = x;
                if( y < minY ) minY = y;
                if( x > maxX ) maxX = x;
                if( y > maxY ) maxY = y;
            }
            if( q[0] || q[1] || q[2] ) nNonBlack++;
        }
    }

    printf( "  %-28s : opaque=%d (alpha=255: %d)  nonBlack=%d  of %d px",
            szLabel, nOpaque, nAlpha255, nNonBlack, w * h );
    if( maxX >= 0 )
        printf( "  bbox=%d,%d..%d,%d\n", minX, minY, maxX, maxY );
    else
        printf( "  bbox=(none: every pixel is fully transparent)\n" );

    free( p );
}

int main( int argc, char** argv )
{
    if( argc < 2 ) { printf( "usage: probe_content.exe <pid>\n" ); return 2; }
    g_pid = (DWORD) atol( argv[1] );

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
        printf( "RESULT: no visible pet window found for pid %lu\n", (unsigned long)g_pid );
        return 1;
    }

    RECT rc = {0,0,0,0};
    GetWindowRect( g_target, &rc );
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    printf( "window %p rect=%ld,%ld %dx%d\n\n", (void*)g_target, rc.left, rc.top, w, h );

    HBITMAP hBmp;

    // --- method 1: the window's own DC -----------------------------------
    {
        HDC hWinDC = GetDC( g_target );
        HDC hMem   = CreateCompatibleDC( NULL );
        hBmp = CreateCompatibleBitmap( hMem, w, h );
        HGDIOBJ hOldM = SelectObject( hMem, hBmp );

        // Fill with a sentinel colour first: if BitBlt does nothing we must not
        // mistake "leftover black" for "the window is black".
        RECT r = { 0, 0, w, h };
        HBRUSH hb = CreateSolidBrush( RGB( 1, 2, 3 ) );
        FillRect( hMem, &r, hb );
        DeleteObject( hb );

        BOOL fOk = BitBlt( hMem, 0, 0, w, h, hWinDC, 0, 0, SRCCOPY );
        printf( "method 1: GetDC(window) + BitBlt -> %s\n", fOk ? "ok" : "FAILED" );
        Analyse( hBmp, w, h, "window DC content" );

        SelectObject( hMem, hOldM );
        DeleteObject( hBmp );
        DeleteDC( hMem );
        ReleaseDC( g_target, hWinDC );
    }

    // --- method 2: PrintWindow with PW_RENDERFULLCONTENT ------------------
    {
        HDC hMem = CreateCompatibleDC( NULL );
        hBmp = CreateCompatibleBitmap( hMem, w, h );
        HGDIOBJ hOldM = SelectObject( hMem, hBmp );

        RECT r = { 0, 0, w, h };
        HBRUSH hb = CreateSolidBrush( RGB( 1, 2, 3 ) );
        FillRect( hMem, &r, hb );
        DeleteObject( hb );

        typedef BOOL (WINAPI *PFN_PW)(HWND,HDC,UINT);
        static PFN_PW pPW = NULL;
        if( !pPW )
        {
            HMODULE hU = LoadLibraryA( "user32.dll" );
            pPW = (PFN_PW) GetProcAddress( hU, "PrintWindow" );
        }
        BOOL fOk = pPW ? pPW( g_target, hMem, 2 /* PW_RENDERFULLCONTENT */ ) : FALSE;
        printf( "\nmethod 2: PrintWindow(PW_RENDERFULLCONTENT) -> %s\n", fOk ? "ok" : "FAILED" );
        Analyse( hBmp, w, h, "PrintWindow content" );

        SelectObject( hMem, hOldM );
        DeleteObject( hBmp );
        DeleteDC( hMem );
    }

    // --- method 3: the whole window is a flat colour? ---------------------
    printf( "\nNote: method 1 returning the sentinel colour (1,2,3 in BGR -> 3,2,1)\n" );
    printf( "      means the layered window has NO readable DC content at all.\n" );
    return 0;
}
