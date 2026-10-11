#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

/*
 verify_pet.cpp - proves on the REAL desktop that the pet actually paints.

 How: find the pet's layered window, capture its rectangle while it is shown,
 hide it, capture the same rectangle again, and compare. The differences are
 exactly the pixels the pet painted. Then judge:
    - a sparse silhouette (well under the rectangle's area) means the sprite
      composites with true transparency, i.e. no background box;
    - a dense block means an opaque rectangle is covering the window;
    - no difference means nothing was painted at all.

 Run it with the pet already running. It restores the window before exiting.
*/

static HWND  g_hPet = NULL;
static RECT  g_rc   = {0,0,0,0};

static BOOL CALLBACK FindProc( HWND h, LPARAM lp )
{
    char szClass[128] = {0};
    GetClassNameA( h, szClass, sizeof(szClass) );
    if( strcmp( szClass, "Neko98TransparentPet" ) == 0 && IsWindowVisible( h ) )
    {
        RECT r;
        if( GetWindowRect( h, &r ) )
        {
            g_hPet = h;
            g_rc = r;
            return FALSE;   //stop
        }
    }
    return TRUE;
}

//capture a region into a freshly allocated 24bpp bottom-up pixel block
static BYTE* Capture( int x, int y, int w, int h )
{
    HDC screen = GetDC( NULL );
    HDC mem = CreateCompatibleDC( screen );
    HBITMAP bmp = CreateCompatibleBitmap( screen, w, h );
    HBITMAP old = (HBITMAP)SelectObject( mem, bmp );

    BitBlt( mem, 0, 0, w, h, screen, x, y, SRCCOPY );

    BITMAPINFO bi;
    ZeroMemory( &bi, sizeof(bi) );
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;      //bottom-up
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    int stride = ((w*3+3)/4)*4;
    BYTE* bits = (BYTE*)malloc( (size_t)stride*h );
    GetDIBits( mem, bmp, 0, h, bits, &bi, DIB_RGB_COLORS );

    SelectObject( mem, old );
    DeleteObject( bmp );
    DeleteDC( mem );
    ReleaseDC( NULL, screen );
    return bits;
}

static BYTE* At( BYTE* p, int stride, int h, int x, int y )
{
    return p + (size_t)(h-1-y)*stride + (size_t)x*3;   //file row 0 is the bottom
}

int main( int argc, char** argv )
{
    //match the real process's coordinate space
    HMODULE u = LoadLibraryA("user32.dll");
    typedef BOOL (WINAPI *PFN)(void*);
    PFN pFn = (PFN)GetProcAddress( u, "SetProcessDpiAwarenessContext" );
    if( pFn ) pFn( (void*)(LONG_PTR)-4 );   //PER_MONITOR_AWARE_V2

    EnumWindows( FindProc, 0 );
    if( !g_hPet )
    {
        printf( "pet window NOT FOUND (is Neko98 running?)\n" );
        return 1;
    }

    int x = g_rc.left, y = g_rc.top;
    int w = g_rc.right - x, h = g_rc.bottom - y;
    printf( "pet window hwnd=%p rect=%d,%d %dx%d\n", (void*)g_hPet, x, y, w, h );

    if( w <= 0 || h <= 0 ) { printf( "window has no size\n" ); return 1; }

    int stride = ((w*3+3)/4)*4;

    BYTE* shown = Capture( x, y, w, h );

    ShowWindow( g_hPet, SW_HIDE );
    Sleep( 500 );                       //let the compositor settle
    BYTE* hidden = Capture( x, y, w, h );
    ShowWindow( g_hPet, SW_SHOWNOACTIVATE );
    Sleep( 200 );

    //The trail fades while we capture, so compare only the pixels that the
    //hidden shot cannot explain: everything drawn by the pet.
    long total = (long)w*h, diff = 0;
    int minx = w, miny = h, maxx = -1, maxy = -1;
    for( int yy = 0; yy < h; yy++ )
    for( int xx = 0; xx < w; xx++ )
    {
        BYTE* a = At( shown,  stride, h, xx, yy );
        BYTE* b = At( hidden, stride, h, xx, yy );
        int d = abs(a[0]-b[0]) + abs(a[1]-b[1]) + abs(a[2]-b[2]);
        if( d > 30 )
        {
            diff++;
            if( xx < minx ) minx = xx;
            if( xx > maxx ) maxx = xx;
            if( yy < miny ) miny = yy;
            if( yy > maxy ) maxy = yy;
        }
    }

    printf( "differing pixels = %ld of %ld (%.1f%% of the window)\n",
            diff, total, 100.0*diff/total );

    if( diff > 0 )
    {
        int bw = maxx-minx+1, bh = maxy-miny+1;
        double pctWindow = 100.0*diff/total;

        printf( "painted bbox = (%d,%d)-(%d,%d) = %dx%d\n", minx, miny, maxx, maxy, bw, bh );

        //The decisive test is how much of the WINDOW the pet filled. An opaque
        //background box would light up essentially every pixel of the surface;
        //a transparent sprite lights up only its own silhouette. (Fill within
        //the silhouette's own bounding box is naturally high for a solid-bodied
        //cat and says nothing about the background.)
        printf( "VERDICT: %s\n",
                pctWindow < 60.0
                  ? "PASS - transparent background (only the sprite's own pixels painted)"
                  : "FAIL - opaque rectangle covering the window" );
    }
    else
    {
        printf( "VERDICT: FAIL - the pet painted nothing\n" );
    }

    free( shown );
    free( hidden );
    return 0;
}
