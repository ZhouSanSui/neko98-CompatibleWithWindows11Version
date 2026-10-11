// TransparentPet.h: a single transparent layered window that FOLLOWS the cat.
//
// Why this class exists (two real bugs it fixes):
//
//  1) SPLIT ACROSS MONITORS. The original CDesktopPet decorated the desktop
//     listview (primary monitor only) while CAlwaysOnTopPet painted on
//     GetDC(NULL) (the whole virtual desktop). The cat and its footprints were
//     therefore rendered on two different surfaces and could land on two
//     different screens. Here the cat AND its footprints are composited into
//     ONE 32-bit premultiplied-ARGB surface inside ONE small window that is
//     moved to follow the cat, so they can never be split.
//
//  2) VISIBLE ICON BACKGROUND. These 1998-era icons are 4bpp DIBs carrying an
//     AND mask and NO alpha channel (verified: Awake.ico is 4bpp, 29% of its
//     mask is transparent). Drawing such an icon into a 32-bit DIB leaves the
//     alpha bytes at 0, so UpdateLayeredWindow(ULW_ALPHA) would composite a
//     fully invisible surface -- and any opaque background baked into the icon
//     shows up. Here alpha is derived from the icon's AND mask (with real icon
//     alpha used when the icon is a modern 32bpp one), so no background is
//     ever drawn and footprints fade away instead of accumulating.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_TRANSPARENTPET_H__A69EBAA0_385D_11D2_9FF9_00001C192944__INCLUDED_)
#define AFX_TRANSPARENTPET_H__A69EBAA0_385D_11D2_9FF9_00001C192944__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include "Pet.h"

//The pet works in "pet world" coordinates: the top-left of the virtual desktop
//is (0,0). That matches all of CNeko's 0-based bounds maths. These helpers
//convert absolute screen coordinates (what GetCursorPos returns) into pet world
//coordinates, which differ whenever the virtual desktop does not start at (0,0)
//(e.g. a secondary monitor placed to the left of / above the primary one).
POINT NekoWorldFromScreen( POINT pt );
void  NekoWorldOrigin( int* px, int* py );
void  NekoWorldSize( int* pcx, int* pcy );


class CTransparentPet : public CPet {

public:
    CTransparentPet( BOOL bAlwaysOnTop = FALSE );
    virtual ~CTransparentPet();

    //override base configuration
    virtual void SetImages( HICON* hIconTable, int nIcons );

    //override base drawing (pure virtual in CPet)
    virtual void Draw( int nImage );
    virtual void Erase();

    //override target drawing -> footprints
    virtual void DrawOnTarget( int x, int y, HICON hIcon );

    //override base visibility: show/hide our own window
    virtual void SetVisible( BOOL bShow );

    //geometry-only refresh (resolution change / monitor hot-plug): keeps the
    //same icons and window, just re-clamps to the new virtual desktop.
    virtual void OnDisplayChange();

    //Repaint coalescing. While a draw batch is open, Draw()/DrawOnTarget() only
    //mark the surface dirty; the single composite happens in FlushDraw(), so
    //"move the cat" + "drop a footprint" in one tick cost ONE UpdateLayeredWindow
    //instead of two. Outside a batch every call composites immediately, which
    //keeps the standalone (non-batched) API callers byte-for-byte correct.
    virtual void BeginDraw();
    virtual void FlushDraw();

protected:
    //a decoded icon (premultiplied ARGB, scaled to the pet size)
    struct SPRITE {
        HICON   hIcon;
        HBITMAP hBmp;
        BYTE*   pBits;
        int     w;
        int     h;
    };

    //a live footprint
    struct FOOTPRINT {
        int   x;
        int   y;
        HICON hIcon;
        DWORD dwBirth;
        //the alpha that was actually composited for this footprint. The fade
        //timer compares it against the current value and skips a composite when
        //nothing has crossed an alpha step yet.
        BYTE  bAlpha;
    };

    //window plumbing
    BOOL CreateLayeredWindow( BOOL bAlwaysOnTop );
    BOOL EnsureSurface( int cx, int cy, BOOL bForce = FALSE );
    void DropSurface();
    static LRESULT CALLBACK WndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

    //rendering helpers
    HBITMAP BuildARGB( HICON hIcon, int w, int h, BYTE** ppBits );
    SPRITE* GetSprite( HICON hIcon, int w, int h );
    void    BlendSprite( int dx, int dy, BYTE* pSrc, int sw, int sh, int alphaMul );
    void    Redraw();
    //Fade-timer entry point: like Redraw(), but it first checks whether any
    //footprint's alpha has moved off the value that was last composited and
    //composites only if so. Returns TRUE when it actually pushed a frame.
    BOOL    RedrawForFade();

    //The pet is PerMonitorV2 DPI-aware, so it must draw bigger pixels on a
    //high-DPI monitor to keep the same apparent size. Sprites are built at that
    //scale and rebuilt when the cat crosses onto a monitor with a different DPI.
    static float DpiScaleForPoint( int x, int y );
    BOOL  RebuildSpritesForScale( float k );
    void  FreeSprites();

    //data
    HWND      m_hWnd;
    HDC       m_hMemDC;
    HBITMAP   m_hMemBmp;
    HBITMAP   m_hOldBmp;    //the DC's original bitmap, restored on teardown
    BYTE*     m_pBits;

    int       m_surfW;      //surface/window size in pixels
    int       m_surfH;
    int       m_cw;         //single icon width (scaled)
    int       m_ch;         //single icon height (scaled)
    int       m_baseW;      //unscaled icon size, before any DPI scaling
    int       m_baseH;
    float     m_fSpriteScale;   //DPI scale the current sprites were built at

    SPRITE*   m_pCatSprite;   //one per cat image (m_nIcons)
    int       m_nCatSprites;  //how many of those were actually built
    FOOTPRINT m_fp[256];
    int       m_nfp;

    SPRITE    m_cache[16];
    int       m_ncache;

    int       m_nCurrentImage;
    BOOL      m_bFadeTimer;
    BOOL      m_bAlwaysOnTop;
    BOOL      m_bWasTopmost;    //z-order currently applied to the window

    //deferred-repaint state: m_bDrawBatch counts nested BeginDraw() calls and
    //m_bDirty records that a composite is still owed.
    int       m_nDrawBatch;
    BOOL      m_bDirty;
    //how many frames have actually been pushed to the screen. Purely
    //instrumentation: it lets the unit test prove that a draw batch collapses
    //two composites into one instead of taking it on faith.
    DWORD     m_nRedraws;

    //dragging: while Windows runs its own modal HTCAPTION move loop we must not
    //let Redraw() re-position the window underneath the user's mouse.
    BOOL      m_bDragging;
    //the cat's top-left offset inside the composited surface (surface -> cat)
    int       m_catOffX;
    int       m_catOffY;

    static const int   MAX_FP      = 256;
    static const DWORD FP_LIFETIME = 2200;  //ms before a footprint fully fades
    //a footprint further than this from the cat is not part of its trail
    static const int   FP_MAX_DIST = 400;
    static const UINT  TIMER_FADE  = 1;
    //fade timer period. FP_LIFETIME/interval = the number of visible fade steps;
    //24 steps over 2.2 s is smooth to the eye and halves the timer wakeups.
    static const UINT  FADE_INTERVAL = 90;
};

#endif // !defined(AFX_TRANSPARENTPET_H__A69EBAA0_385D_11D2_9FF9_00001C192944__INCLUDED_)
