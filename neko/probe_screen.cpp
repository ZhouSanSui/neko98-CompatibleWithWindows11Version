// probe_screen.cpp — dump the actual screen pixels over the pet's rect as ASCII art.
//
// This is the ground truth that no window-state API can fake: what the monitor
// is really showing inside the pet's window rectangle.  A live cat shows a paw/
// body silhouette; an invisible pet shows pure background.
//
// Usage: probe_screen.exe <pid> [gapMs]
// Prints the rect, then two ASCII snapshots separated by gapMs (default 900),
// so a MOVING cat is distinguishable from a frozen or empty one.
//
// Build: g++ -O2 -o probe_screen.exe probe_screen.cpp -lgdi32 -luser32
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

static BYTE* Grab( const RECT* prc, int* pw, int* ph )
{
    int w = prc->right - prc->left;
    int h = prc->bottom - prc->top;
    if( w <= 0 || h <= 0 ) return NULL;

    HDC hScreen = GetDC( NULL );
    HDC hMem    = CreateCompatibleDC( hScreen );
    HBITMAP hBmp = CreateCompatibleBitmap( hScreen, w, h );
    HGDIOBJ hOld = SelectObject( hMem, hBmp );
    BOOL fOk = BitBlt( hMem, 0, 0, w, h, hScreen, prc->left, prc->top, SRCCOPY );

    BITMAPINFO bi;
    memset( &bi, 0, sizeof bi );
    bi.bmiHeader.biSize  = sizeof( BITMAPINFOHEADER );
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight= -h;
    bi.bmiHeader.biPlanes= 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    int stride = ( ( w * 3 + 3 ) / 4 ) * 4;
    BYTE* p = (BYTE*) malloc( (size_t)stride * h );
    if( p && fOk && GetDIBits( hMem, hBmp, 0, h, p, &bi, DIB_RGB_COLORS ) != h )
    {
        free( p ); p = NULL;
    }

    SelectObject( hMem, hOld );
    DeleteObject( hBmp );
    DeleteDC( hMem );
    ReleaseDC( NULL, hScreen );

    if( p ) { *pw = w; *ph = h; }
    return p;
}

// Print one snapshot.  Buckets by luminance so the silhouette is visible in text.
static int Dump( const BYTE* p, int w, int h, const char* szLabel, int nCols )
{
    printf( "--- %s ---\n", szLabel );

    // Establish the background colour as the most common corner-ish value.
    const BYTE* tl = p;                                   // top-left pixel
    int refL = ( tl[2] * 30 + tl[1] * 59 + tl[0] * 11 ) / 100;

    int stepX = w / nCols; if( stepX < 1 ) stepX = 1;
    int stepY = stepX * 2;                                // characters are ~2:1

    int nDark = 0, nTotal = 0;
    for( int y = 0; y < h; y += stepY )
    {
        for( int x = 0; x < w; x += stepX )
        {
            int stride = ( ( w * 3 + 3 ) / 4 ) * 4;
            const BYTE* q = p + (size_t)y * stride + x * 3;
            int b = q[0], g = q[1], r = q[2];
            int L = ( r * 30 + g * 59 + b * 11 ) / 100;
            int d = abs( L - refL );
            char c = '.';
            if( d > 12 )  c = '-';
            if( d > 40 )  c = '+';
            if( d > 80 )  c = '#';
            if( d > 140 ) c = '@';
            putchar( c );
            nTotal++;
            if( d > 40 ) nDark++;
        }
        putchar( '\n' );
    }

    printf( "(pixels differing from the reference luminance: %d of %d sampled = %.1f%%)\n\n",
            nDark, nTotal, 100.0 * nDark / nTotal );
    return nDark;
}

int main( int argc, char** argv )
{
    if( argc < 2 ) { printf( "usage: probe_screen.exe <pid> [gapMs]\n" ); return 2; }
    g_pid = (DWORD) atol( argv[1] );
    int nGap = ( argc >= 3 ) ? atoi( argv[2] ) : 900;

    typedef BOOL (WINAPI *PFN_SET)(HANDLE);
    HMODULE hUser = LoadLibraryA( "user32.dll" );
    if( hUser )
    {
        PFN_SET pSet = (PFN_SET) GetProcAddress( hUser, "SetProcessDpiAwarenessContext" );
        if( pSet ) pSet( (HANDLE)(LONG_PTR)-4 );
    }

    EnumWindows( FindProc, 0 );
    if( !g_target ) { printf( "no pet window\n" ); return 1; }

    RECT rc = {0,0,0,0};
    GetWindowRect( g_target, &rc );
    printf( "pet window rect = %ld,%ld  %ldx%ld   (gap=%dms)\n\n",
            rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, nGap );

    int w = 0, h = 0;
    BYTE* a = Grab( &rc, &w, &h );
    if( !a ) { printf( "capture failed\n" ); return 2; }
    int nDarkA = Dump( a, w, h, "snapshot A (now)", 64 );

    Sleep( nGap );

    RECT rc2 = {0,0,0,0};
    GetWindowRect( g_target, &rc2 );
    int moved = ( rc2.left != rc.left || rc2.top != rc.top ||
                  rc2.right != rc.right || rc2.bottom != rc.bottom );

    int w2 = 0, h2 = 0;
    BYTE* b = Grab( &rc2, &w2, &h2 );
    int nDarkB = -1, nChanged = -1;
    if( b )
    {
        nDarkB = Dump( b, w2, h2, "snapshot B (after gap)", 64 );
        if( w2 == w && h2 == h )
        {
            int stride = ( ( w * 3 + 3 ) / 4 ) * 4;
            nChanged = 0;
            for( int i = 0; i < stride * h; i++ )
                if( a[i] != b[i] ) nChanged++;
            printf( "raw bytes differing between A and B: %d of %d\n", nChanged, stride * h );
        }
    }

    printf( "\n=== summary ===\n" );
    printf( "window rect %s during the gap\n", moved ? "MOVED (the pet is animating)" : "did NOT move" );
    printf( "reference-luminance deviation: A=%.1f%%  B=%.1f%%\n",
            100.0 * nDarkA / 64, nDarkB < 0 ? -1.0 : 100.0 * nDarkB / 64 );
    if( nDarkA < 20 )
        printf( "VERDICT: the rect is essentially FLAT BACKGROUND -> the cat is NOT on screen.\n" );
    else
        printf( "VERDICT: there is real content inside the rect -> the cat IS on screen.\n" );

    free( a );
    if( b ) free( b );
    return 0;
}
