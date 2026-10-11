// TransparentPet.cpp: implementation of the CTransparentPet class.
//
// Two real bugs are fixed here.
//
//  BUG 1 - "cat and footprints are on two different screens".
//  The original renderers drew the cat and the footprints onto two DIFFERENT
//  surfaces: CDesktopPet decorated the desktop listview (primary monitor only)
//  while CAlwaysOnTopPet painted the cat in its own top-level window but the
//  footprints with DrawIconEx(GetDC(NULL), ...) which is the PRIMARY monitor's
//  DC. As soon as the cat moved to the secondary monitor, its footprints were
//  still painted on the primary one. Fix: composite the cat AND its footprints
//  into a single 32-bit surface belonging to a single window, and place that
//  window on the monitor the cat is on.
//
//  BUG 2 - "the footprint background is not hidden".
//  The shipped icons are 1998-era 4bpp DIBs that carry an AND mask and NO alpha
//  channel (verified: Awake.ico is 4bpp with 29% of its mask transparent,
//  fp_up.ico is 4bpp with 46% transparent). DrawIconEx into a 32-bit DIB leaves
//  every alpha byte at 0, so UpdateLayeredWindow(ULW_ALPHA) composited a fully
//  invisible surface, and painting straight onto a screen DC drew the icon's
//  opaque background box. Fix: derive alpha from the icon's AND mask (falling
//  back to the real alpha channel for modern 32bpp icons) and always composite
//  with premultiplied ARGB through UpdateLayeredWindow.
//
//////////////////////////////////////////////////////////////////////

#include "TransparentPet.h"

#include <stdlib.h>
#include <string.h>
#include <new>          //std::nothrow
#include <windowsx.h>   //GET_X_LPARAM / GET_Y_LPARAM (WM_NCHITTEST)

extern HINSTANCE g_hInstance;

//window class name
static const char* g_szPetClassName = "Neko98TransparentPet";

//track whether the class has been registered (only once per process)
static BOOL g_fPetClassRegistered = FALSE;

//////////////////////////////////////////////////////////////////////
// Pet-world coordinate helpers
//
// CNeko does all of its maths with 0-based bounds (it compares positions
// against 0 and against (bounds.right - bounds.left)). That is a "pet world"
// whose origin is the top-left of the VIRTUAL desktop. GetCursorPos(), on the
// other hand, returns absolute screen coordinates, which differ from pet-world
// coordinates whenever the virtual desktop does not start at (0,0) - exactly
// the case when a second monitor sits to the LEFT of or ABOVE the primary one.
// On such a setup the original code handed raw screen coordinates to
// RunTowards(), so the cat chased a target that was offset by the virtual
// origin, which is why the trail and the cat diverged.
//////////////////////////////////////////////////////////////////////

void NekoWorldOrigin( int* px, int* py )
{
    if( px ) *px = GetSystemMetrics( SM_XVIRTUALSCREEN );
    if( py ) *py = GetSystemMetrics( SM_YVIRTUALSCREEN );
}

void NekoWorldSize( int* pcx, int* pcy )
{
    int cx = GetSystemMetrics( SM_CXVIRTUALSCREEN );
    int cy = GetSystemMetrics( SM_CYVIRTUALSCREEN );

    //fall back to the primary monitor if the virtual metrics are unavailable
    if( cx <= 0 ) cx = GetSystemMetrics( SM_CXSCREEN );
    if( cy <= 0 ) cy = GetSystemMetrics( SM_CYSCREEN );

    if( pcx ) *pcx = cx;
    if( pcy ) *pcy = cy;
}

POINT NekoWorldFromScreen( POINT pt )
{
    int ox, oy;
    NekoWorldOrigin( &ox, &oy );
    pt.x -= ox;
    pt.y -= oy;
    return pt;
}


//////////////////////////////////////////////////////////////////////
// Small local helpers
//////////////////////////////////////////////////////////////////////

//Read a device-dependent bitmap into a freshly allocated top-down 32bpp BGRA
//buffer. For a monochrome (AND mask) bitmap, GetDIBits expands it to 32bpp
//where 0 = black (opaque) and 255 = white (transparent).
static BYTE* ReadBitmap32( HBITMAP hbm, int* pw, int* ph )
{
    if( !hbm ) return NULL;

    BITMAP bm = {0};
    if( !GetObject( hbm, sizeof(bm), &bm ) ) return NULL;
    if( bm.bmWidth <= 0 || bm.bmHeight <= 0 ) return NULL;

    int bw = bm.bmWidth;
    int bh = bm.bmHeight;
    if( (size_t)bw * bh > 64u * 1024u * 1024u ) return NULL;   //sanity guard

    BYTE* buf = (BYTE*)malloc( (size_t)bw * bh * 4 );
    if( !buf ) return NULL;

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = bw;
    bmi.bmiHeader.biHeight      = bh;      //bottom-up: universally accepted
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC( NULL );
    int ok = GetDIBits( hdc, hbm, 0, bh, buf, &bmi, DIB_RGB_COLORS );
    ReleaseDC( NULL, hdc );

    if( !ok )
    {
        free( buf );
        return NULL;
    }

    if( pw ) *pw = bw;
    if( ph ) *ph = bh;
    return buf;
}

//Fetch the DPI scale of the monitor containing a pet-world point, used to keep
//the cat looking the same physical size on every display. This machine can
//easily have a 100% primary and a 150% secondary, and because the binary is
//PerMonitorV2 aware the OS will NOT stretch our window for us - we must size
//the sprite ourselves or the cat would look small on the high-DPI screen.
float CTransparentPet::DpiScaleForPoint( int x, int y )
{
    int ox, oy;
    NekoWorldOrigin( &ox, &oy );

    POINT pt;
    pt.x = x + ox;
    pt.y = y + oy;

    HMONITOR hm = MonitorFromPoint( pt, MONITOR_DEFAULTTONEAREST );

    //Resolve the DPI entry points ONCE. This function is called on every frame
    //(Redraw) and from SetImages, so loading shcore.dll on each call was pure
    //overhead; the module is deliberately never freed.
    typedef HRESULT (WINAPI *PFN_GetDpiForMonitor)( HMONITOR, int, UINT*, UINT* );
    typedef UINT    (WINAPI *PFN_GetDpiForWindow)( HWND );

    static BOOL                 bDpiInit = FALSE;
    static HMODULE              hShcore  = NULL;
    static PFN_GetDpiForMonitor pGetDpiForMonitor = NULL;
    static PFN_GetDpiForWindow  pGetDpiForWindow  = NULL;

    if( !bDpiInit )
    {
        bDpiInit = TRUE;

        hShcore = LoadLibraryA( "shcore.dll" );
        if( hShcore )
            pGetDpiForMonitor = (PFN_GetDpiForMonitor)GetProcAddress( hShcore, "GetDpiForMonitor" );

        //GetDpiForWindow shipped later than GetDpiForMonitor (Win10 1607), so it
        //is the fallback when shcore.dll / GetDpiForMonitor is unavailable.
        HMODULE hUser = GetModuleHandleA( "user32.dll" );
        if( hUser )
            pGetDpiForWindow = (PFN_GetDpiForWindow)GetProcAddress( hUser, "GetDpiForWindow" );
    }

    int dpi = 96;
    if( pGetDpiForMonitor )
    {
        UINT dx = 96, dy = 96;
        if( SUCCEEDED( pGetDpiForMonitor( hm, 0, &dx, &dy ) ) && dx >= 96 )   //0 = MDT_EFFECTIVE_DPI
            dpi = (int)dx;
    }
    else if( pGetDpiForWindow )
    {
        //No monitor-level API: fall back to the window that hosts our surface.
        HWND hWnd = WindowFromPoint( pt );
        if( hWnd )
        {
            UINT d = pGetDpiForWindow( hWnd );
            if( d >= 96 ) dpi = (int)d;
        }
    }

    if( dpi < 96 ) dpi = 96;

    float k = (float)dpi / 96.0f;
    if( k < 1.0f ) k = 1.0f;
    if( k > 4.0f ) k = 4.0f;
    return k;
}


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CTransparentPet::CTransparentPet( BOOL bAlwaysOnTop )
{
    m_hWnd        = NULL;
    m_hMemDC      = NULL;
    m_hMemBmp     = NULL;
    m_hOldBmp     = NULL;
    m_pBits       = NULL;
    m_surfW       = 0;
    m_surfH       = 0;
    m_cw          = 32;
    m_ch          = 32;
    m_baseW       = 32;
    m_baseH       = 32;
    m_fSpriteScale = 1.0f;
    m_pCatSprite  = NULL;
    m_nCatSprites = 0;
    m_nfp         = 0;
    m_ncache      = 0;
    m_nCurrentImage = -1;
    m_bFadeTimer  = FALSE;
    m_bAlwaysOnTop = bAlwaysOnTop ? TRUE : FALSE;
    //CreateLayeredWindow() applies WS_EX_TOPMOST while creating the window, so
    //record that up front: the first Redraw() must not think a z-order change
    //is still pending.
    m_bWasTopmost  = m_bAlwaysOnTop;

    m_bDragging   = FALSE;
    m_catOffX     = 0;
    m_catOffY     = 0;

    //No draw batch is open and nothing is owed yet.
    m_nDrawBatch  = 0;
    m_bDirty      = FALSE;
    m_nRedraws    = 0;

    memset( m_cache, 0, sizeof(m_cache) );
    memset( m_fp, 0, sizeof(m_fp) );
    m_nfp = 0;

    //The pet roams the whole virtual desktop in 0-based "pet world"
    //coordinates, exactly like the original program did.
    int vw, vh;
    NekoWorldSize( &vw, &vh );
    SetRect( &m_rcBounds, 0, 0, vw, vh );

    CreateLayeredWindow( bAlwaysOnTop );
}


CTransparentPet::~CTransparentPet()
{
    if( m_hWnd )
    {
        KillTimer( m_hWnd, TIMER_FADE );
        DestroyWindow( m_hWnd );
    }
    DropSurface();

    FreeSprites();
}


BOOL CTransparentPet::CreateLayeredWindow( BOOL bAlwaysOnTop )
{
    if( !g_fPetClassRegistered )
    {
        WNDCLASS wc = {0};
        wc.style         = 0;
        wc.lpfnWndProc   = WndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = g_hInstance;
        wc.hIcon         = NULL;
        wc.hCursor       = NULL;
        wc.hbrBackground = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = g_szPetClassName;

        if( !RegisterClass( &wc ) ) return FALSE;
        g_fPetClassRegistered = TRUE;
    }

    //WS_EX_LAYERED     : per-pixel alpha surface
    //WS_EX_TRANSPARENT : click-through - essential, the window can cover a
    //                    large part of a monitor and must never eat clicks
    //WS_EX_TOOLWINDOW  : keep it out of the taskbar / alt-tab
    //WS_EX_NOACTIVATE  : never steal focus from the user's work
    DWORD dwExStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if( bAlwaysOnTop ) dwExStyle |= WS_EX_TOPMOST;

    m_hWnd = CreateWindowEx( dwExStyle,
                             g_szPetClassName,
                             "Neko",
                             WS_POPUP,
                             0, 0, 1, 1,
                             NULL, NULL, g_hInstance, (LPVOID)this );
    if( !m_hWnd ) return FALSE;

    //start with a surface the size of one icon so the very first Draw works
    EnsureSurface( m_cw, m_ch );

    //UpdateLayeredWindow() does not make a window visible by itself, so show it
    //once here; the first Redraw() immediately moves and sizes it.
    ShowWindow( m_hWnd, SW_SHOWNOACTIVATE );

    return TRUE;
}


BOOL CTransparentPet::EnsureSurface( int cx, int cy, BOOL bForce )
{
    if( cx <= 0 ) cx = 1;
    if( cy <= 0 ) cy = 1;

    //already big enough? (the surface only ever grows, to avoid reallocating it
    //on every animation frame as the cat walks around)
    if( !bForce && m_hMemBmp && m_pBits && m_surfW >= cx && m_surfH >= cy )
        return TRUE;

    DropSurface();

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = cx;
    bmi.bmiHeader.biHeight      = -cy;     //top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC( NULL );
    m_hMemBmp = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, (void**)&m_pBits, NULL, 0 );
    ReleaseDC( NULL, hdc );
    if( !m_hMemBmp || !m_pBits )
    {
        DropSurface();
        return FALSE;
    }

    m_hMemDC = CreateCompatibleDC( NULL );
    if( !m_hMemDC )
    {
        DropSurface();
        return FALSE;
    }

    m_hOldBmp = (HBITMAP)SelectObject( m_hMemDC, m_hMemBmp );
    if( !m_hOldBmp )
    {
        //the DC never took our bitmap: tear the whole thing down again so the
        //destructor / DropSurface() always see a consistent state.
        DropSurface();
        return FALSE;
    }

    m_surfW = cx;
    m_surfH = cy;
    return TRUE;
}


void CTransparentPet::DropSurface()
{
    if( m_hMemDC )
    {
        //restore the bitmap the DC was originally holding before dropping it
        if( m_hOldBmp ) SelectObject( m_hMemDC, m_hOldBmp );
        m_hOldBmp = NULL;

        DeleteDC( m_hMemDC );
        m_hMemDC = NULL;
    }
    if( m_hMemBmp )
    {
        DeleteObject( m_hMemBmp );
        m_hMemBmp = NULL;
    }
    m_pBits = NULL;
    m_surfW = 0;
    m_surfH = 0;
}


LRESULT CALLBACK CTransparentPet::WndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    if( uMsg == WM_CREATE )
    {
        CREATESTRUCT* pcs = (CREATESTRUCT*)lParam;
        SetWindowLongPtr( hWnd, GWLP_USERDATA, (LONG_PTR)pcs->lpCreateParams );
        return 0;
    }

    CTransparentPet* pPet = (CTransparentPet*)GetWindowLongPtr( hWnd, GWLP_USERDATA );
    if( !pPet ) return DefWindowProc( hWnd, uMsg, wParam, lParam );

    switch( uMsg )
    {
        case WM_ERASEBKGND:
            return TRUE;    //never erase: the surface is fully repainted

        //Alpha-aware hit testing. Without this the layered window's whole
        //rectangle swallows the mouse because WS_EX_TRANSPARENT makes the
        //compositor ignore input while the WINDOW still covers its pixels, so
        //even the fully transparent corners would steal a click. Return
        //HTTRANSPARENT unless the cursor is really over an opaque pixel.
        case WM_NCHITTEST:
        {
            if( !pPet->m_pBits || pPet->m_surfW <= 0 || pPet->m_surfH <= 0 )
                return HTTRANSPARENT;

            RECT rc;
            if( !GetWindowRect( hWnd, &rc ) ) return HTTRANSPARENT;

            int sx = GET_X_LPARAM( lParam ) - rc.left;
            int sy = GET_Y_LPARAM( lParam ) - rc.top;

            if( sx < 0 || sy < 0 || sx >= pPet->m_surfW || sy >= pPet->m_surfH )
                return HTTRANSPARENT;

            BYTE a = pPet->m_pBits[((size_t)sy * pPet->m_surfW + sx) * 4 + 3];
            return ( a > 32 ) ? HTCLIENT : HTTRANSPARENT;
        }

        //HTCLIENT above lets the client area see WM_LBUTTONDOWN; hand the drag
        //to Windows' own modal HTCAPTION move loop.
        case WM_LBUTTONDOWN:
            ReleaseCapture();
            SendMessage( hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0 );
            return 0;

        //The HTCAPTION move loop is running: freeze our own painting so
        //Redraw() cannot snap the window back under the user's mouse.
        case WM_ENTERSIZEMOVE:
            pPet->m_bDragging = TRUE;
            return 0;

        case WM_EXITSIZEMOVE:
            pPet->m_bDragging = FALSE;
            //The user left the window somewhere new: tell the pet where it is so
            //it keeps walking from there instead of snapping back next frame.
            if( pPet->m_surfW > 0 && pPet->m_surfH > 0 )
            {
                RECT rc;
                if( GetWindowRect( hWnd, &rc ) )
                {
                    int wx, wy;
                    NekoWorldOrigin( &wx, &wy );

                    POINT pt;
                    pt.x = rc.left + pPet->m_catOffX;
                    pt.y = rc.top  + pPet->m_catOffY;

                    pPet->m_ptPosition.x = pt.x - wx;
                    pPet->m_ptPosition.y = pt.y - wy;
                    //keep the trail/prune maths consistent: anything keyed off
                    //the old position must not think the cat teleported.
                    pPet->m_ptOldPosition.x = pPet->m_ptPosition.x;
                    pPet->m_ptOldPosition.y = pPet->m_ptPosition.y;
                }
            }
            return 0;

        //The window changed monitors / the display DPI changed: drop the cached
        //scale so the next Redraw() rebuilds the sprites at the new DPI. 0 can
        //never equal a real scale, and Redraw() rebuilds when they differ.
        case WM_DPICHANGED:
            pPet->m_fSpriteScale = 0.0f;
            pPet->Redraw();
            return 0;

        case WM_TIMER:
            if( wParam == TIMER_FADE )
            {
                //RedrawForFade() composites only when a footprint's alpha has
                //actually moved off the value that was last blended, so the
                //many ticks that would repaint an identical frame are free.
                pPet->RedrawForFade();
                //stop the timer once the trail has completely faded away
                if( pPet->m_nfp == 0 )
                {
                    KillTimer( hWnd, TIMER_FADE );
                    pPet->m_bFadeTimer = FALSE;
                }
            }
            return 0;
    }

    return DefWindowProc( hWnd, uMsg, wParam, lParam );
}


//////////////////////////////////////////////////////////////////////
// Image preparation
//////////////////////////////////////////////////////////////////////

void CTransparentPet::SetImages( HICON* hIconTable, int nIcons )
{
    //the base class copies the icons and works out the unscaled image size
    CPet::SetImages( hIconTable, nIcons );

    //forget any base size from a previous configuration; the DPI-aware base is
    //the size CPet just computed (which already includes the user's zoom).
    m_baseW = m_sizeImage.cx;
    m_baseH = m_sizeImage.cy;

    //build the sprites at the DPI of wherever the pet currently is
    float k = DpiScaleForPoint( m_ptPosition.x, m_ptPosition.y );
    RebuildSpritesForScale( k );
}


//Release the decoded cat sprites and the footprint sprite cache. Both are
//DPI-dependent, so they are thrown away whenever the scale changes.
void CTransparentPet::FreeSprites()
{
    if( m_pCatSprite )
    {
        //m_nIcons is the number of images CPet knows about, which can be larger
        //than the number of sprites we actually managed to build; walk the real
        //count or we would read (and DeleteObject) uninitialised slots.
        for( int i = 0; i < m_nCatSprites; i++ )
            if( m_pCatSprite[i].hBmp ) DeleteObject( m_pCatSprite[i].hBmp );
        delete[] m_pCatSprite;
        m_pCatSprite = NULL;
    }
    m_nCatSprites = 0;
    for( int i = 0; i < m_ncache; i++ )
    {
        if( m_cache[i].hBmp ) DeleteObject( m_cache[i].hBmp );
        memset( &m_cache[i], 0, sizeof(m_cache[i]) );
    }
    m_ncache = 0;
}


BOOL CTransparentPet::RebuildSpritesForScale( float k )
{
    if( m_nIcons <= 0 || !m_hIcons ) return FALSE;

    FreeSprites();

    //Unscaled base size. CPet::SetImages() put it in m_sizeImage, already folded
    //with the user's own zoom setting; capture it once so rebuilding at a new
    //DPI scale never multiplies the scale twice.
    int w = m_baseW > 0 ? m_baseW : ( m_sizeImage.cx > 0 ? m_sizeImage.cx : 32 );
    int h = m_baseH > 0 ? m_baseH : ( m_sizeImage.cy > 0 ? m_sizeImage.cy : 32 );
    m_baseW = w;
    m_baseH = h;

    if( k < 1.0f ) k = 1.0f;
    if( k > 4.0f ) k = 4.0f;

    if( k != 1.0f )
    {
        w = (int)(w * k + 0.5f);
        h = (int)(h * k + 0.5f);
    }
    if( w < 1 ) w = 1;
    if( h < 1 ) h = 1;

    m_sizeImage.cx = w;
    m_sizeImage.cy = h;
    m_cw = w;
    m_ch = h;
    m_fSpriteScale = k;

    EnsureSurface( w, h );

    //Build into a local first: if anything goes wrong we must leave the object
    //in a consistent state (and the caller already handles a FALSE return).
    SPRITE* pSprites = new (std::nothrow) SPRITE[m_nIcons];
    if( !pSprites )
    {
        m_pCatSprite  = NULL;
        m_nCatSprites = 0;
        return FALSE;
    }

    int nBuilt = 0;
    int nDecoded = 0;
    for( int i = 0; i < m_nIcons; i++ )
    {
        pSprites[i].hIcon = m_hIcons[i];
        pSprites[i].hBmp  = BuildARGB( m_hIcons[i], w, h, &pSprites[i].pBits );
        pSprites[i].w     = w;
        pSprites[i].h     = h;
        nBuilt++;                       //slot is initialised, even if decoding failed
        if( pSprites[i].hBmp && pSprites[i].pBits ) nDecoded++;
    }

    //Nothing decoded at all is not a usable configuration either - free what we
    //built so the next Redraw() does not walk a half-initialised array.
    if( nDecoded <= 0 )
    {
        for( int i = 0; i < nBuilt; i++ )
            if( pSprites[i].hBmp ) DeleteObject( pSprites[i].hBmp );
        delete[] pSprites;
        m_pCatSprite  = NULL;
        m_nCatSprites = 0;
        return FALSE;
    }

    m_pCatSprite  = pSprites;
    m_nCatSprites = nBuilt;             //how many slots FreeSprites may touch
    return TRUE;
}


//Decode one icon into our own premultiplied-ARGB sprite.
//
//This is the heart of BUG 2's fix. DrawIconEx alone is not enough: for these
//4bpp icons the destination alpha channel is never written, so the icon's
//background would either be fully transparent (invisible pet) or fully opaque
//(visible background box). The icon's AND mask is the authoritative shape, so
//we use it to build alpha, and use the colour bitmap only for RGB.
HBITMAP CTransparentPet::BuildARGB( HICON hIcon, int w, int h, BYTE** ppBits )
{
    if( ppBits ) *ppBits = NULL;
    if( !hIcon || w <= 0 || h <= 0 ) return NULL;

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;      //top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC( NULL );
    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0 );
    ReleaseDC( NULL, hdc );
    if( !hBmp || !pBits ) return NULL;

    BYTE* p = (BYTE*)pBits;
    memset( p, 0, (size_t)w * h * 4 );

    //---- 1. RGB: DrawIconEx does the palette work and any scaling for us ----
    HDC mem = CreateCompatibleDC( NULL );
    HBITMAP hOld = (HBITMAP)SelectObject( mem, hBmp );
    DrawIconEx( mem, 0, 0, hIcon, w, h, 0, NULL, DI_NORMAL );
    SelectObject( mem, hOld );
    DeleteDC( mem );

    //---- 2. alpha from the icon's AND mask (and real alpha if present) ----
    ICONINFO ii = {0};
    if( GetIconInfo( hIcon, &ii ) )
    {
        int mw = 0, mh = 0;
        BYTE* pMask = ReadBitmap32( ii.hbmMask, &mw, &mh );

        int cw = 0, ch = 0;
        BYTE* pColor = ReadBitmap32( ii.hbmColor, &cw, &ch );

        //Does this icon carry a real 32bpp alpha channel? (modern icons do;
        //the shipped 1998 4bpp icons do not - their alpha bytes are all zero.)
        BOOL fHasAlpha = FALSE;
        if( pColor )
        {
            BITMAP bmC = {0};
            if( GetObject( ii.hbmColor, sizeof(bmC), &bmC ) && bmC.bmBitsPixel == 32 )
            {
                for( int i = 0; i < cw * ch; i++ )
                    if( pColor[i * 4 + 3] ) { fHasAlpha = TRUE; break; }
            }
        }

        for( int y = 0; y < h; y++ )
        {
            BYTE* pRow = p + (size_t)y * w * 4;
            for( int x = 0; x < w; x++ )
            {
                BYTE a = 255;

                //AND mask: 1 = transparent. Nearest-neighbour when scaled.
                if( pMask && mw > 0 && mh > 0 )
                {
                    int sx = (int)((x * (long)mw) / w);
                    int sy = (int)((y * (long)mh) / h);
                    if( sx >= mw ) sx = mw - 1;
                    if( sy >= mh ) sy = mh - 1;
                    //buffer is bottom-up
                    BYTE mv = pMask[((size_t)(mh - 1 - sy) * mw + sx) * 4];
                    if( mv != 0 ) a = 0;    //white in the mask == transparent
                }

                //if the icon really has alpha, prefer it
                if( a != 0 && fHasAlpha && pColor && cw > 0 && ch > 0 )
                {
                    int sx = (int)((x * (long)cw) / w);
                    int sy = (int)((y * (long)ch) / h);
                    if( sx >= cw ) sx = cw - 1;
                    if( sy >= ch ) sy = ch - 1;
                    a = pColor[((size_t)(ch - 1 - sy) * cw + sx) * 4 + 3];
                }

                BYTE* o = pRow + (size_t)x * 4;
                if( a == 0 )
                {
                    o[0] = o[1] = o[2] = o[3] = 0;
                }
                else
                {
                    //premultiply: UpdateLayeredWindow() requires premultiplied ARGB
                    o[0] = (BYTE)(((int)o[0] * a) / 255);   //blue
                    o[1] = (BYTE)(((int)o[1] * a) / 255);   //green
                    o[2] = (BYTE)(((int)o[2] * a) / 255);   //red
                    o[3] = a;
                }
            }
        }

        if( pMask )  free( pMask );
        if( pColor ) free( pColor );
        if( ii.hbmMask )  DeleteObject( ii.hbmMask );
        if( ii.hbmColor ) DeleteObject( ii.hbmColor );
    }

    if( ppBits ) *ppBits = p;
    return hBmp;
}


CTransparentPet::SPRITE* CTransparentPet::GetSprite( HICON hIcon, int w, int h )
{
    //A cached sprite is only valid for the size it was decoded at: the cat can
    //cross onto a monitor with a different DPI while a trail is still fading, so
    //the same icon can legitimately need two different sizes.
    for( int i = 0; i < m_ncache; i++ )
        if( m_cache[i].hIcon == hIcon && m_cache[i].w == w && m_cache[i].h == h )
            return &m_cache[i];

    //Cache full: evict the oldest entry instead of failing. Returning NULL here
    //used to silently drop every footprint once the 16 slots were taken.
    if( m_ncache >= 16 )
    {
        if( m_cache[0].hBmp ) DeleteObject( m_cache[0].hBmp );
        memmove( &m_cache[0], &m_cache[1], sizeof(SPRITE) * (16 - 1) );
        memset( &m_cache[15], 0, sizeof(SPRITE) );
        m_ncache = 15;
    }

    SPRITE* s = &m_cache[m_ncache];
    s->hIcon = hIcon;
    s->hBmp  = BuildARGB( hIcon, w, h, &s->pBits );
    s->w     = w;
    s->h     = h;
    m_ncache++;
    return s;
}


//////////////////////////////////////////////////////////////////////
// Compositing
//////////////////////////////////////////////////////////////////////

//Composite a premultiplied-ARGB sprite onto the premultiplied-ARGB surface.
//(dx,dy) are surface-local pixel coordinates and may be negative.
void CTransparentPet::BlendSprite( int dx, int dy, BYTE* pSrc, int sw, int sh, int alphaMul )
{
    if( !pSrc || !m_pBits ) return;

    int x0 = dx < 0 ? 0 : dx;
    int y0 = dy < 0 ? 0 : dy;
    int x1 = dx + sw; if( x1 > m_surfW ) x1 = m_surfW;
    int y1 = dy + sh; if( y1 > m_surfH ) y1 = m_surfH;
    if( x1 <= x0 || y1 <= y0 ) return;

    if( alphaMul < 0 )   alphaMul = 0;
    if( alphaMul > 255 ) alphaMul = 255;

    for( int y = y0; y < y1; y++ )
    {
        BYTE* pDst = m_pBits + ((size_t)y * m_surfW + x0) * 4;
        BYTE* pS   = pSrc   + ((size_t)(y - dy) * sw + (x0 - dx)) * 4;

        for( int x = x0; x < x1; x++ )
        {
            //The sprite is premultiplied, so its colour already carries pS[3].
            //Fading with alphaMul must therefore scale the SOURCE COLOUR once,
            //not twice: the old code multiplied pS[0..2] by alphaMul again in
            //the non-255 branch (and not in the >=255 branch), so faded
            //footprints darkened while they faded.
            int sa = ((int)pS[3] * alphaMul) / 255;
            if( sa <= 0 ) { pDst += 4; pS += 4; continue; }

            int inv = 255 - sa;

            int sr = ((int)pS[2] * alphaMul + 127) / 255;   //red
            int sg = ((int)pS[1] * alphaMul + 127) / 255;   //green
            int sb = ((int)pS[0] * alphaMul + 127) / 255;   //blue

            pDst[0] = (BYTE)(sb + ((int)pDst[0] * inv + 127) / 255);
            pDst[1] = (BYTE)(sg + ((int)pDst[1] * inv + 127) / 255);
            pDst[2] = (BYTE)(sr + ((int)pDst[2] * inv + 127) / 255);
            pDst[3] = (BYTE)(sa + ((int)pDst[3] * inv + 127) / 255);

            pDst += 4;
            pS   += 4;
        }
    }
}


//////////////////////////////////////////////////////////////////////
// Placement + painting
//////////////////////////////////////////////////////////////////////

//Which monitor should host the pet? Pick the one covering most of the cat, so
//the cat is clipped as little as possible when it straddles a seam.
static RECT MonitorRectForCat( int cx, int cy, int cw, int ch )
{
    int ox, oy;
    NekoWorldOrigin( &ox, &oy );

    //sample the four corners and the centre of the cat, in screen coordinates
    POINT samples[5];
    samples[0].x = cx;         samples[0].y = cy;
    samples[1].x = cx + cw - 1; samples[1].y = cy;
    samples[2].x = cx;         samples[2].y = cy + ch - 1;
    samples[3].x = cx + cw - 1; samples[3].y = cy + ch - 1;
    samples[4].x = cx + cw / 2; samples[4].y = cy + ch / 2;

    HMONITOR hBest = NULL;
    int nBest = 0;
    HMONITOR hSeen[8];
    int nSeen = 0;

    for( int i = 0; i < 5; i++ )
    {
        POINT pt;
        pt.x = samples[i].x + ox;
        pt.y = samples[i].y + oy;

        HMONITOR hm = MonitorFromPoint( pt, MONITOR_DEFAULTTONEAREST );

        BOOL fNew = TRUE;
        int nThis = 0;
        for( int j = 0; j < nSeen; j++ )
        {
            if( hSeen[j] == hm ) { fNew = FALSE; nThis = j; break; }
        }
        if( fNew && nSeen < 8 )
        {
            hSeen[nSeen] = hm;
            nThis = nSeen;
            nSeen++;
            //count how many samples land on this monitor
            int nCount = 0;
            for( int k = 0; k < 5; k++ )
            {
                POINT p2;
                p2.x = samples[k].x + ox;
                p2.y = samples[k].y + oy;
                if( MonitorFromPoint( p2, MONITOR_DEFAULTTONEAREST ) == hm ) nCount++;
            }
            if( nCount > nBest ) { nBest = nCount; hBest = hm; }
        }
    }

    if( !hBest )
    {
        POINT ptZero = {0, 0};
        hBest = MonitorFromPoint( ptZero, MONITOR_DEFAULTTOPRIMARY );
    }

    MONITORINFO mi = {0};
    mi.cbSize = sizeof(mi);
    RECT rc;
    if( GetMonitorInfo( hBest, &mi ) )
        rc = mi.rcMonitor;
    else
        SetRect( &rc, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) );

    //convert the monitor rect from screen coordinates into pet world
    rc.left   -= ox;
    rc.top    -= oy;
    rc.right  -= ox;
    rc.bottom -= oy;
    return rc;
}


void CTransparentPet::Redraw()
{
    if( !m_hWnd ) return;

    //While the user drags the pet with the HTCAPTION move loop, Windows owns the
    //window position. Painting here would re-position (and possibly re-size) the
    //window under the mouse on every tick, fighting the drag.
    if( m_bDragging ) return;

    int cw = m_cw > 0 ? m_cw : 32;
    int ch = m_ch > 0 ? m_ch : 32;

    //---- which monitor is the cat on? ----
    RECT mon = MonitorRectForCat( m_ptPosition.x, m_ptPosition.y, cw, ch );

    //If the cat has wandered onto a display with a different DPI, rebuild the
    //sprites at the new scale so it keeps the same apparent size. (Being
    //PerMonitorV2 aware means the OS will not do this for us.)
    {
        float kNow = DpiScaleForPoint( m_ptPosition.x, m_ptPosition.y );
        if( kNow != m_fSpriteScale && m_nIcons > 0 )
        {
            RebuildSpritesForScale( kNow );
            cw = m_cw > 0 ? m_cw : 32;
            ch = m_ch > 0 ? m_ch : 32;
            mon = MonitorRectForCat( m_ptPosition.x, m_ptPosition.y, cw, ch );
        }
    }

    //---- prune the trail BEFORE sizing anything ----
    //Expired footprints are dropped, and so is anything too far from the cat to
    //plausibly be a trail it just left (e.g. after it teleported to the other
    //monitor). Doing this first is what keeps the surface bounded: otherwise a
    //single stale footprint could stretch the bounding box across the whole
    //virtual desktop and every frame would composite a screen-sized surface.
    DWORD now = GetTickCount();
    {
        int keep = 0;
        for( int i = 0; i < m_nfp; i++ )
        {
            if( now - m_fp[i].dwBirth >= FP_LIFETIME ) continue;

            long dx = (long)m_fp[i].x - m_ptPosition.x;
            long dy = (long)m_fp[i].y - m_ptPosition.y;
            if( dx < 0 ) dx = -dx;
            if( dy < 0 ) dy = -dy;

            //also require the footprint to be on the cat's monitor, so the cat
            //and its whole trail are always shown on the SAME screen
            if( dx > FP_MAX_DIST || dy > FP_MAX_DIST ) continue;
            if( m_fp[i].x + cw < mon.left || m_fp[i].x > mon.right ) continue;
            if( m_fp[i].y + ch < mon.top  || m_fp[i].y > mon.bottom ) continue;

            m_fp[keep++] = m_fp[i];
        }
        m_nfp = keep;
    }

    //---- bounding box of the cat plus the surviving footprints ----
    int minX = m_ptPosition.x;
    int minY = m_ptPosition.y;
    int maxX = m_ptPosition.x + cw;
    int maxY = m_ptPosition.y + ch;

    for( int i = 0; i < m_nfp; i++ )
    {
        if( m_fp[i].x < minX ) minX = m_fp[i].x;
        if( m_fp[i].y < minY ) minY = m_fp[i].y;
        if( m_fp[i].x + cw > maxX ) maxX = m_fp[i].x + cw;
        if( m_fp[i].y + ch > maxY ) maxY = m_fp[i].y + ch;
    }

    //Keep the window on the cat's monitor: this is what guarantees the cat and
    //its footprints are always displayed on the SAME screen.
    if( minX < mon.left )        minX = mon.left;
    if( minY < mon.top )         minY = mon.top;
    if( maxX > mon.right )       maxX = mon.right;
    if( maxY > mon.bottom )      maxY = mon.bottom;

    //round the surface up a little so it does not have to be reallocated on
    //every single frame while the cat walks
    int needW = maxX - minX;
    int needH = maxY - minY;
    if( needW < cw ) needW = cw;
    if( needH < ch ) needH = ch;

    int surfW = (needW + 63) & ~63;
    int surfH = (needH + 63) & ~63;

    //never let the padded surface spill off the monitor
    int monW = mon.right - mon.left;
    int monH = mon.bottom - mon.top;
    if( surfW > monW ) surfW = monW;
    if( surfH > monH ) surfH = monH;
    if( surfW < needW ) surfW = needW;
    if( surfH < needH ) surfH = needH;

    //The surface only ever grows, so moving from a large monitor to a smaller
    //one would leave a surface too big to fit. Force a rebuild in that case.
    //Also shrink back down if the surface has grown far larger than this frame
    //needs (a long trail can balloon it), otherwise it would stay huge forever.
    BOOL bForce = FALSE;
    if( m_surfW > monW || m_surfH > monH ) bForce = TRUE;
    else if( m_hMemBmp )
    {
        long long cur  = (long long)m_surfW * m_surfH;
        long long need = (long long)surfW * surfH;
        //Shrink back as soon as the allocated surface is more than 1.5x what
        //this frame needs (and the excess is worth reclaiming at all), instead
        //of waiting for a 4x blow-up the trail would have to grow into.
        if( cur > need + 4096 && cur > (need * 3) / 2 ) bForce = TRUE;
    }
    if( !EnsureSurface( surfW, surfH, bForce ) ) return;

    //Recompute the placement from the size the surface ACTUALLY has. The
    //surface only ever grows, so it may well be larger than what this frame
    //needs; positioning by the requested size would leave the window (and thus
    //the visible pixels) offset from where the cat really is.
    int winX = minX;
    int winY = minY;
    if( winX + m_surfW > mon.right )  winX = mon.right - m_surfW;
    if( winY + m_surfH > mon.bottom ) winY = mon.bottom - m_surfH;
    if( winX < mon.left ) winX = mon.left;
    if( winY < mon.top )  winY = mon.top;

    //Remember where the cat sits inside the surface we are about to composite.
    //WM_EXITSIZEMOVE needs exactly this offset to turn the dragged window rect
    //back into a pet-world position.
    m_catOffX = m_ptPosition.x - winX;
    m_catOffY = m_ptPosition.y - winY;

    //---- paint ----
    memset( m_pBits, 0, (size_t)m_surfW * m_surfH * 4 );

    //footprints first (oldest at the bottom), then the cat on top.
    //The trail was already pruned above, so every remaining one is drawn.
    int nDrawn = 0;
    for( int i = 0; i < m_nfp; i++ )
    {
        DWORD age = now - m_fp[i].dwBirth;

        int alpha = (int)((FP_LIFETIME - age) * 255 / FP_LIFETIME);
        if( alpha < 0 )   alpha = 0;
        if( alpha > 255 ) alpha = 255;

        SPRITE* s = GetSprite( m_fp[i].hIcon, cw, ch );
        if( s && s->pBits )
            BlendSprite( m_fp[i].x - winX, m_fp[i].y - winY, s->pBits, cw, ch, alpha );

        //remember what we just composited so the fade timer can tell a real
        //alpha change from a tick that would redraw the identical frame
        m_fp[i].bAlpha = (BYTE)alpha;

        nDrawn++;
    }
    (void)nDrawn;

    if( m_nCurrentImage >= 0 && m_nCurrentImage < m_nIcons && m_pCatSprite )
    {
        SPRITE* s = &m_pCatSprite[m_nCurrentImage];
        if( s->pBits )
            BlendSprite( m_ptPosition.x - winX, m_ptPosition.y - winY, s->pBits, cw, ch, 255 );
    }

    //---- push the surface to the screen ----
    //The window is placed in absolute screen coordinates, while our surface
    //coordinates are pet-world, hence the world origin offset.
    //
    //CRITICAL: move + resize + content update must be ATOMIC. The previous
    //version called SetWindowPos() first and UpdateLayeredWindow() afterwards,
    //which leaves a window whose position/size already describe THIS frame while
    //its layered content is still the LAST frame's bitmap. The compositor can
    //pick up that intermediate state, and because the surface is re-anchored
    //(and sometimes reallocated) on many frames, the result is the visible
    //"cat and footprints jump" artefact. Passing the destination rectangle to
    //UpdateLayeredWindow() itself (pptDst != NULL) performs position, size and
    //content in a single call, so they can never disagree.
    int ox, oy;
    NekoWorldOrigin( &ox, &oy );

    POINT ptDst = { winX + ox, winY + oy };
    POINT ptSrc = {0, 0};
    SIZE  sz    = { m_surfW, m_surfH };
    BLENDFUNCTION bf;
    bf.BlendOp             = AC_SRC_OVER;
    bf.BlendFlags          = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat         = AC_SRC_ALPHA;

    //Z-order is the one property UpdateLayeredWindow cannot change, so apply it
    //only when it actually differs from what is already in effect.
    if( m_bAlwaysOnTop != m_bWasTopmost )
    {
        SetWindowPos( m_hWnd, m_bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                      0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER );
        m_bWasTopmost = m_bAlwaysOnTop;
    }

    UpdateLayeredWindow( m_hWnd, NULL, &ptDst, &sz, m_hMemDC, &ptSrc, 0, &bf, ULW_ALPHA );

    //instrumentation only (the unit test reads this through an accessor)
    m_nRedraws++;
}


//////////////////////////////////////////////////////////////////////
// Base-class overrides
//////////////////////////////////////////////////////////////////////

void CTransparentPet::Draw( int nImage )
{
    m_nCurrentImage = nImage;

    //Inside a draw batch the composite is deferred to FlushDraw(), so a tick
    //that both moves the cat and drops a footprint pushes ONE frame instead of
    //two. Standalone callers (tests, one-shot draws) are unaffected because
    //they never open a batch and still composite immediately.
    if( m_nDrawBatch > 0 )
    {
        m_bDirty = TRUE;
        return;
    }

    Redraw();
}


//Fade-timer entry point. The fade timer wakes far more often than the alpha
//values actually change (24 visible steps over FP_LIFETIME), so compositing on
//every tick would push mostly identical frames. Only a footprint whose alpha has
//moved to a different rounded value can change the picture, so check first.
BOOL CTransparentPet::RedrawForFade()
{
    if( !m_hWnd )                    return FALSE;
    if( m_bDragging )                return FALSE;
    if( m_nDrawBatch > 0 ) { m_bDirty = TRUE; return FALSE; }

    DWORD now = GetTickCount();
    for( int i = 0; i < m_nfp; i++ )
    {
        DWORD age = now - m_fp[i].dwBirth;

        //An expired footprint changes the picture too - Redraw() is what prunes
        //it from the trail, and without this the timer could never stop.
        if( age >= FP_LIFETIME )
        {
            Redraw();
            return TRUE;
        }

        int alpha = (int)((FP_LIFETIME - age) * 255 / FP_LIFETIME);
        if( alpha < 0 )   alpha = 0;
        if( alpha > 255 ) alpha = 255;

        if( (BYTE)alpha != m_fp[i].bAlpha )
        {
            Redraw();
            return TRUE;
        }
    }

    return FALSE;
}


//BeginDraw / FlushDraw - coalesce a burst of drawing into a single composite.
void CTransparentPet::BeginDraw()
{
    m_nDrawBatch++;
}


void CTransparentPet::FlushDraw()
{
    if( m_nDrawBatch > 0 ) m_nDrawBatch--;

    //still inside a nested batch: the outermost one owns the composite
    if( m_nDrawBatch > 0 ) return;

    if( !m_bDirty ) return;
    m_bDirty = FALSE;

    Redraw();
}


//A display-mode change (resolution flip, monitor hot-plug, colour depth) alters
//the geometry but not the user's configuration, so respond by re-deriving the
//geometry instead of rebuilding every cat from the registry.
void CTransparentPet::OnDisplayChange()
{
    if( !m_hWnd ) return;

    //re-read the virtual desktop and re-anchor the pet world
    int vw, vh;
    NekoWorldSize( &vw, &vh );
    if( vw > 0 && vh > 0 ) SetRect( &m_rcBounds, 0, 0, vw, vh );

    //the surface may be sized for a monitor that no longer exists (or may have
    //been reallocated at the old resolution), so drop it and let Redraw() build
    //a correctly sized one
    DropSurface();

    //0 can never equal a real scale, and Redraw() rebuilds the sprites whenever
    //they differ - this forces a rebuild at the new DPI.
    m_fSpriteScale = 0.0f;

    //If the cat now sits outside the desktop, pull it back in; otherwise it
    //could be left permanently invisible after a resolution reduction.
    if( m_rcBounds.right > 0 && m_ptPosition.x + m_cw > m_rcBounds.right )
        m_ptPosition.x = m_rcBounds.right - m_cw;
    if( m_rcBounds.bottom > 0 && m_ptPosition.y + m_ch > m_rcBounds.bottom )
        m_ptPosition.y = m_rcBounds.bottom - m_ch;
    if( m_ptPosition.x < m_rcBounds.left ) m_ptPosition.x = m_rcBounds.left;
    if( m_ptPosition.y < m_rcBounds.top )  m_ptPosition.y = m_rcBounds.top;

    Redraw();
}


void CTransparentPet::Erase()
{
    //Nothing to erase: the whole surface is recomposited on every Draw(), and
    //footprints are deliberately kept so they can fade out.
}


//Show/hide the pet's own window. Hiding changes no other state; showing
//re-composites so the layered content is valid again (UpdateLayeredWindow does
//not make a window visible, and a hidden window's surface may be stale).
void CTransparentPet::SetVisible( BOOL bShow )
{
    if( !m_hWnd ) return;

    ShowWindow( m_hWnd, bShow ? SW_SHOWNOACTIVATE : SW_HIDE );
    if( bShow ) Redraw();
}


void CTransparentPet::DrawOnTarget( int x, int y, HICON hIcon )
{
    if( !hIcon ) return;

    //drop the oldest footprint when the ring is full
    if( m_nfp >= MAX_FP )
    {
        memmove( &m_fp[0], &m_fp[1], sizeof(FOOTPRINT) * (MAX_FP - 1) );
        m_nfp = MAX_FP - 1;
    }

    m_fp[m_nfp].x       = x;
    m_fp[m_nfp].y       = y;
    m_fp[m_nfp].hIcon   = hIcon;
    m_fp[m_nfp].dwBirth = GetTickCount();
    m_fp[m_nfp].bAlpha  = 255;      //freshly dropped: fully opaque
    m_nfp++;

    //run a timer while the trail is alive so the footprints actually fade
    if( m_hWnd && !m_bFadeTimer )
    {
        SetTimer( m_hWnd, TIMER_FADE, FADE_INTERVAL, NULL );
        m_bFadeTimer = TRUE;
    }

    if( m_nDrawBatch > 0 )
    {
        m_bDirty = TRUE;
        return;
    }

    Redraw();
}
