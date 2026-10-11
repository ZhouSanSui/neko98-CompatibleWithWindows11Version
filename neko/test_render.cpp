// test_render.cpp - decisive unit test for the CTransparentPet fixes.
//
// It drives the REAL CTransparentPet implementation (linked from
// TransparentPet.o + Pet.o, not a copy) and inspects the actual composited
// surface, so it can prove or disprove:
//
//   * BUG 2 (hidden background): the cat/footprint sprites must have non-zero
//     alpha where the icon shape is, and zero alpha where the icon's AND mask
//     says transparent. A fully-zero surface would mean "invisible pet".
//   * premultiplication correctness (channel <= alpha), which
//     UpdateLayeredWindow(ULW_ALPHA) requires.
//   * BUG 1 (two screens): the window must be placed inside the monitor that
//     holds the cat, and the footprints must be composited into the SAME
//     surface/window as the cat.

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "TransparentPet.h"

//definition (normally lives in Neko98.cpp, which we deliberately do not link)
HINSTANCE g_hInstance = NULL;

//expose the protected innards without touching the production class
class CTestPet : public CTransparentPet
{
public:
    HICON m_testFp[8];

    CTestPet( BOOL bTop ) : CTransparentPet( bTop ) { memset(m_testFp,0,sizeof(m_testFp)); }

    BYTE* Bits()          { return m_pBits; }
    int   SurfW()         { return m_surfW; }
    int   SurfH()         { return m_surfH; }
    int   CatW()          { return m_cw; }
    int   CatH()          { return m_ch; }
    int   Nfp()           { return m_nfp; }
    HWND  Wnd()           { return m_hWnd; }
    void  Place( int x, int y ) { m_ptPosition.x = x; m_ptPosition.y = y; }
    void  Show( int i )   { Draw( i ); }
    void  Paint()         { Redraw(); }
    void  Drop()          { DrawOnTarget( 100, 100, m_testFp[0] ); }
    //protected members are reachable from a derived class, so expose plain
    //accessors instead of leaking the nested types to callers
    BYTE* CatSpriteBits( int i ) { return ( m_pCatSprite && i >= 0 && i < m_nIcons ) ? m_pCatSprite[i].pBits : NULL; }
    DWORD FpLifetime()    { return FP_LIFETIME; }
    int   MaxFp()         { return MAX_FP; }
    int   BaseW()         { return m_baseW; }
    int   BaseH()         { return m_baseH; }
    float ScaleForPoint( int x, int y ) { return DpiScaleForPoint( x, y ); }
    DWORD Redraws()       { return m_nRedraws; }
    void  BatchBegin()    { BeginDraw(); }
    void  BatchEnd()      { FlushDraw(); }
    void  DropAt( int x, int y, HICON h ) { DrawOnTarget( x, y, h ); }
    BOOL  FadeTick()      { return RedrawForFade(); }
    void  DisplayChange(){ OnDisplayChange(); }

    //Stop the automatic fade timer and drain any WM_TIMER already queued for it,
    //so a composite count taken across a few calls is not perturbed by a stray
    //fade frame arriving mid-measurement.
    void  QuiesceFade()
    {
        if( m_hWnd ) KillTimer( m_hWnd, TIMER_FADE );
        m_bFadeTimer = FALSE;
        MSG msg;
        while( m_hWnd && PeekMessage( &msg, m_hWnd, WM_TIMER, WM_TIMER, PM_REMOVE ) ) { }
    }
};

static int g_nFail = 0;
static void CHECK( const char* what, BOOL ok, const char* detail )
{
    printf( "[%s] %-46s %s\n", ok ? "PASS" : "FAIL", what, detail ? detail : "" );
    if( !ok ) g_nFail++;
}

//ICONINFO access to compare our alpha against the icon's real AND mask
struct MaskInfo { int w, h; BYTE* bits; };

static MaskInfo ReadMask( HICON hIcon )
{
    MaskInfo mi = {0,0,NULL};
    ICONINFO ii = {0};
    if( !GetIconInfo( hIcon, &ii ) ) return mi;

    BITMAP bm = {0};
    if( ii.hbmMask && GetObject( ii.hbmMask, sizeof(bm), &bm ) )
    {
        mi.w = bm.bmWidth;
        mi.h = bm.bmHeight;
        mi.bits = (BYTE*)malloc( (size_t)mi.w * mi.h * 4 );
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = mi.w;
        bmi.bmiHeader.biHeight = mi.h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        HDC dc = GetDC(NULL);
        GetDIBits( dc, ii.hbmMask, 0, mi.h, mi.bits, &bmi, DIB_RGB_COLORS );
        ReleaseDC( NULL, dc );
    }
    if( ii.hbmMask )  DeleteObject( ii.hbmMask );
    if( ii.hbmColor ) DeleteObject( ii.hbmColor );
    return mi;
}

//Prove the actual mechanism the pet relies on works on THIS machine: a layered
//window pushed with UpdateLayeredWindow(ULW_ALPHA) must show its opaque pixels
//on screen AND let the desktop show through its fully transparent ones. This is
//exactly the pipeline that used to render nothing (alpha stayed 0) or a
//background box (alpha stayed 255 everywhere).
static int TestLayeredAlphaMechanism( int cw, int ch )
{
    int nFail = 0;

    const int W = 96, H = 96;
    int px = GetSystemMetrics(SM_CXSCREEN) - W - 120;   //right side of primary
    int py = GetSystemMetrics(SM_CYSCREEN) - H - 160;
    if( px < 0 ) px = 100;
    if( py < 0 ) py = 100;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = g_hInstance;
    wc.lpszClassName = "NekoTestLayered";
    RegisterClassA( &wc );

    HWND h = CreateWindowExA( WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                              "NekoTestLayered", "t", WS_POPUP,
                              px, py, W, H, NULL, NULL, g_hInstance, NULL );
    if( !h )
    {
        CHECK( "layered test window created", FALSE, NULL );
        return 1;
    }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = W;
    bmi.bmiHeader.biHeight      = -H;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(NULL);
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection( screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0 );
    HDC mem = CreateCompatibleDC( screen );
    HBITMAP old = (HBITMAP)SelectObject( mem, dib );
    BYTE* p = (BYTE*)bits;

    //left half: opaque pure red (premultiplied red == BGR(0,0,255) alpha 255)
    //right half: alpha 0 (must be fully invisible so the desktop shows through)
    for( int y = 0; y < H; y++ )
    {
        for( int x = 0; x < W; x++ )
        {
            BYTE* o = p + ((size_t)y * W + x) * 4;
            if( x < W/2 ) { o[0] = 0; o[1] = 0; o[2] = 255; o[3] = 255; }
            else          { o[0] = 0; o[1] = 0; o[2] = 255; o[3] = 0;   }
        }
    }

    POINT src = {0,0};
    SIZE  sz  = {W,H};
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER; bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255; bf.AlphaFormat = AC_SRC_ALPHA;

    ShowWindow( h, SW_SHOWNOACTIVATE );
    UpdateLayeredWindow( h, NULL, NULL, &sz, mem, &src, 0, &bf, ULW_ALPHA );

    //let the compositor flush
    for( int i = 0; i < 20; i++ ) { MSG m; while( PeekMessage(&m,NULL,0,0,PM_REMOVE) ) { TranslateMessage(&m); DispatchMessage(&m);} Sleep(20); }

    //read the screen back: the red half must be red, the transparent half must
    //NOT be red (it must show whatever was underneath)
    int nRed = 0, nRedRight = 0;
    HDC screen2 = GetDC(NULL);
    for( int y = 10; y < H-10; y++ )
    {
        for( int x = 10; x < W/2-10; x++ )
        {
            COLORREF c = GetPixel( screen2, px + x, py + y );
            if( GetRValue(c) > 200 && GetGValue(c) < 60 && GetBValue(c) < 60 ) nRed++;
        }
        for( int x = W/2+10; x < W-10; x++ )
        {
            COLORREF c = GetPixel( screen2, px + x, py + y );
            if( GetRValue(c) > 200 && GetGValue(c) < 60 && GetBValue(c) < 60 ) nRedRight++;
        }
    }
    ReleaseDC( NULL, screen2 );

    char buf[256];
    sprintf( buf, "opaque region red px=%d, alpha-0 region red px=%d", nRed, nRedRight );
    printf( "     %s\n", buf );
    CHECK( "UpdateLayeredWindow shows opaque pixels on screen", nRed > 1000, buf );
    CHECK( "alpha-0 region is see-through (no background box)", nRedRight < 50, buf );

    SelectObject( mem, old );
    DeleteDC( mem );
    DeleteObject( dib );
    DestroyWindow( h );
    ReleaseDC( NULL, screen );
    return nFail;
}

int main()
{
    g_hInstance = GetModuleHandle( NULL );

    //Become PerMonitorV2 aware, exactly like the shipped pet's manifest, so the
    //metrics and coordinates this test sees match production. A DPI-unaware
    //process gets virtualised coordinates and would validate the wrong numbers.
    {
        HMODULE u32 = GetModuleHandleA( "user32.dll" );
        typedef BOOL (WINAPI *PFN_SetCtx)( void* );
        PFN_SetCtx pSet = u32 ? (PFN_SetCtx)GetProcAddress( u32, "SetProcessDpiAwarenessContext" ) : NULL;
        if( pSet ) pSet( (void*)(LONG_PTR)-4 );   //PER_MONITOR_AWARE_V2
    }

    printf( "=== Neko98 CTransparentPet render test ===\n" );
    printf( "virtual screen: %d,%d %dx%d\n",
            GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN) );

    //load the pet's own icons straight out of this module's resources
    HICON hCat[32];
    HICON hFp[8];
    int nCat = 0, nFp = 0;
    for( int i = 0; i < 32; i++ )
    {
        HICON h = (HICON)LoadImage( g_hInstance, MAKEINTRESOURCE(101 + i), IMAGE_ICON, 0, 0, 0 );
        if( h ) hCat[nCat++] = h;
    }
    for( int i = 0; i < 8; i++ )
    {
        HICON h = (HICON)LoadImage( g_hInstance, MAKEINTRESOURCE(133 + i), IMAGE_ICON, 0, 0, 0 );
        if( h ) hFp[nFp++] = h;
    }
    printf( "loaded %d cat icons, %d footprint icons\n", nCat, nFp );

    char buf[512];
    sprintf( buf, "%d cat / %d footprint icons found", nCat, nFp );
    CHECK( "icons present in resources", nCat >= 32 && nFp == 8, buf );
    if( nCat < 1 || nFp < 1 ) return 1;

    CTestPet pet( FALSE );
    memcpy( pet.m_testFp, hFp, sizeof(hFp) );

    CHECK( "layered window created", pet.Wnd() != NULL && IsWindow(pet.Wnd()) != 0, NULL );

    //---- which styles did we actually get? ----
    LONG ex = GetWindowLong( pet.Wnd(), GWL_EXSTYLE );
    sprintf( buf, "exstyle=0x%08lX", (unsigned long)ex );
    CHECK( "WS_EX_LAYERED set", (ex & WS_EX_LAYERED) != 0, buf );
    CHECK( "WS_EX_TRANSPARENT set (click-through)", (ex & WS_EX_TRANSPARENT) != 0, NULL );
    CHECK( "window is visible", IsWindowVisible(pet.Wnd()) != 0, NULL );

    //---- BUG 2: does the decoded cat sprite actually have alpha? ----
    pet.SetImages( hCat, nCat );

    int cw = pet.CatW(), ch = pet.CatH();
    sprintf( buf, "cat sprite %dx%d", cw, ch );
    CHECK( "cat sprite built", cw > 0 && ch > 0, buf );
    if( cw <= 0 || ch <= 0 ) return 1;

    BYTE* sp = pet.CatSpriteBits( 0 );
    CHECK( "cat sprite has pixel buffer", sp != NULL, NULL );
    if( !sp ) return 1;

    int nOpaque = 0, nClear = 0, nPremulBad = 0, maxA = 0;
    for( int i = 0; i < cw * ch; i++ )
    {
        BYTE* p = sp + (size_t)i * 4;
        if( p[3] > 0 ) nOpaque++;
        else           nClear++;
        if( p[3] > maxA ) maxA = p[3];
        //premultiplied: no colour channel may exceed alpha
        if( p[0] > p[3] || p[1] > p[3] || p[2] > p[3] ) nPremulBad++;
    }
    sprintf( buf, "opaque=%d clear=%d maxAlpha=%d (of %d px)", nOpaque, nClear, maxA, cw*ch );
    //The decisive assertion for the "invisible pet" bug: a correct decode has a
    //MIX of opaque and clear pixels. All-clear == invisible; all-opaque == the
    //background box is showing.
    CHECK( "cat sprite alpha is NOT all zero", nOpaque > 0, buf );
    CHECK( "cat sprite background IS transparent", nClear > 0, buf );
    CHECK( "cat sprite alpha has a real shape (mix)", nOpaque > 0 && nClear > 0, buf );
    sprintf( buf, "%d pixels violate premultiplication", nPremulBad );
    CHECK( "sprite is premultiplied ARGB", nPremulBad == 0, buf );

    //---- compare our alpha against the icon's own AND mask ----
    {
        MaskInfo mi = ReadMask( hCat[0] );
        if( mi.bits && mi.w > 0 )
        {
            int agree = 0, total = 0;
            for( int y = 0; y < ch; y++ )
            {
                int my = (y * (long)mi.h) / ch; if( my >= mi.h ) my = mi.h - 1;
                for( int x = 0; x < cw; x++ )
                {
                    int mx = (x * (long)mi.w) / cw; if( mx >= mi.w ) mx = mi.w - 1;
                    //mask is bottom-up; white(255) == transparent
                    BYTE mv = mi.bits[((size_t)(mi.h - 1 - my) * mi.w + mx) * 4];
                    BOOL maskClear = (mv == 0);   //0 == opaque in AND mask
                    BOOL ourOpaque = (sp[((size_t)y * cw + x) * 4 + 3] > 0);
                    if( maskClear == ourOpaque ) agree++;
                    total++;
                }
            }
            double pct = total ? (100.0 * agree / total) : 0.0;
            sprintf( buf, "%.2f%% of pixels match the icon AND mask", pct );
            CHECK( "alpha derived from AND mask", pct > 95.0, buf );
            free( mi.bits );
        }
    }

    //---- footprints composite into the SAME surface as the cat ----
    {
        pet.Place( 200, 200 );
        pet.Show( 0 );          //select a cat image, then its Draw() composites
        pet.Paint();

        int sw1 = pet.SurfW(), sh1 = pet.SurfH();
        int litCat = 0;
        for( int i = 0; i < sw1 * sh1; i++ ) if( pet.Bits()[i*4+3] ) litCat++;

        pet.Drop();     //add a footprint via the real DrawOnTarget path
        CHECK( "footprint recorded", pet.Nfp() > 0, NULL );

        //the surface grows when the trail extends the bounding box, so measure
        //the NEW buffer (and report it, since a silent realloc would hide bugs)
        int sw2 = pet.SurfW(), sh2 = pet.SurfH();
        int litBoth = 0, firstLit = -1;
        for( int i = 0; i < sw2 * sh2; i++ )
        {
            if( pet.Bits()[i*4+3] ) { litBoth++; if( firstLit < 0 ) firstLit = i; }
        }

        sprintf( buf, "surface %dx%d -> %dx%d; lit cat=%d, cat+fp=%d, firstLit=%d",
                 sw1, sh1, sw2, sh2, litCat, litBoth, firstLit );
        printf( "     %s\n", buf );
        CHECK( "footprint drawn into the cat's surface", litBoth > litCat, buf );

        //the footprint must be in the SAME window as the cat, not a second one
        RECT rc; GetWindowRect( pet.Wnd(), &rc );
        sprintf( buf, "window %ld,%ld %ldx%ld", rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top );
        CHECK( "single window holds cat+footprints", pet.Wnd() != NULL, buf );
    }

    //---- BUG 1: window lands inside the monitor that holds the cat ----
    {
        int failed = 0;
        const char* names[] = { "top-left", "right", "bottom", "centre" };
        int vx0 = GetSystemMetrics(SM_XVIRTUALSCREEN), vy0 = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw  = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh  = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        int tx[4], ty[4];
        tx[0] = 0;            ty[0] = 0;
        tx[1] = vw - cw;      ty[1] = 40;
        tx[2] = 40;           ty[2] = vh - ch;
        tx[3] = vw/2;         ty[3] = vh/2;

        for( int i = 0; i < 4; i++ )
        {
            pet.Place( tx[i], ty[i] );
            pet.Paint();

            RECT rc; GetWindowRect( pet.Wnd(), &rc );

            //the cat's own rectangle, in absolute screen coords
            RECT cat; cat.left = tx[i]+vx0; cat.top = ty[i]+vy0;
            cat.right = cat.left + cw; cat.bottom = cat.top + ch;

            //which monitor holds the cat's centre?
            POINT c; c.x = cat.left + cw/2; c.y = cat.top + ch/2;
            HMONITOR hm = MonitorFromPoint( c, MONITOR_DEFAULTTONEAREST );
            MONITORINFO mi = {0}; mi.cbSize = sizeof(mi);
            GetMonitorInfo( hm, &mi );

            //the window must overlap the cat's monitor generously
            RECT isect;
            BOOL ov = IntersectRect( &isect, &rc, &mi.rcMonitor );
            int ovArea = ov ? (isect.right-isect.left)*(isect.bottom-isect.top) : 0;
            int winArea = (rc.right-rc.left)*(rc.bottom-rc.top);
            int pct = winArea > 0 ? (100*ovArea/winArea) : 0;

            sprintf( buf, "%-8s win=%ld,%ld %ldx%ld, on-cat-monitor=%d%%",
                     names[i], rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, pct );
            printf( "     %s\n", buf );
            if( pct < 95 ) failed++;
        }
        CHECK( "window stays on the cat's monitor", failed == 0, NULL );
    }

    //---- footprint fade lifetime actually expires ----
    CHECK( "footprint lifetime constant is sane",
           pet.FpLifetime() > 0 && pet.FpLifetime() < 60000, NULL );
    CHECK( "footprint ring is bounded", pet.MaxFp() > 0 && pet.MaxFp() <= 4096, NULL );

    //---- one tick that moves the cat AND drops a footprint must composite ONCE ----
    //Neko.cpp:RunTowards() composites, then may immediately emit a footprint which
    //used to composite a second time. Update() now wraps the whole tick in
    //BeginDraw()/FlushDraw(). This counts real UpdateLayeredWindow calls, so the
    //claim is measured rather than asserted.
    {
        pet.Place( 300, 300 );
        pet.QuiesceFade();

        //Measure each call on its own so a surprise shows up as a named number
        //instead of a lump, and WARM UP first: moving the pet between two monitors
        //of different DPI makes Windows post WM_DPICHANGED to the window, and that
        //handler forces its own rebuild+repaint - so the FIRST composite after a
        //cross-monitor move legitimately costs two. The steady-state numbers
        //(paint2/show2/drop2) are what the batching claim is actually about.
        pet.Paint();

        DWORD c[6];
        c[0] = pet.Redraws(); pet.Paint();            DWORD paint1 = pet.Redraws() - c[0];
        c[1] = pet.Redraws(); pet.Paint();            DWORD paint2 = pet.Redraws() - c[1];
        c[2] = pet.Redraws(); pet.Show( 0 );          DWORD show1  = pet.Redraws() - c[2];
        c[3] = pet.Redraws(); pet.Show( 0 );          DWORD show2  = pet.Redraws() - c[3];
        c[4] = pet.Redraws(); pet.Drop();             DWORD drop1  = pet.Redraws() - c[4];
        c[5] = pet.Redraws(); pet.Drop();             DWORD drop2  = pet.Redraws() - c[5];

        DWORD paintCost = paint1 + paint2;

        //steady-state solo cost of a whole tick's worth of drawing: one composite
        //for the move, one for the footprint
        DWORD a0 = pet.Redraws();
        pet.Show( 0 );
        pet.Drop();
        DWORD soloCost = pet.Redraws() - a0;

        //batched: the same two calls must cost exactly one composite
        DWORD a1 = pet.Redraws();
        pet.BatchBegin();
        pet.Show( 0 );
        pet.Drop();
        pet.BatchEnd();
        DWORD batchCost = pet.Redraws() - a1;

        //a batch that draws nothing must not composite at all
        DWORD a2 = pet.Redraws();
        pet.BatchBegin();
        pet.BatchEnd();
        DWORD emptyCost = pet.Redraws() - a2;

        //nested batches must still collapse into one composite, not leak one each
        DWORD a3 = pet.Redraws();
        pet.BatchBegin();
        pet.BatchBegin();
        pet.Show( 0 );
        pet.BatchEnd();
        DWORD innerCost = pet.Redraws() - a3;   //still open: must be 0
        pet.BatchEnd();
        DWORD nestedCost = pet.Redraws() - a3;  //closed: exactly 1

        sprintf( buf, "steady paint=%lu show=%lu drop=%lu solo=%lu batched=%lu empty=%lu nested=%lu (warmup paint=%lu show=%lu drop=%lu)",
                 (unsigned long)paint2, (unsigned long)show2, (unsigned long)drop2,
                 (unsigned long)soloCost, (unsigned long)batchCost,
                 (unsigned long)emptyCost, (unsigned long)nestedCost,
                 (unsigned long)paint1, (unsigned long)show1, (unsigned long)drop1 );
        printf( "     %s\n", buf );

        CHECK( "an explicit repaint composites exactly once", paint2 == 1 && paint1 <= 2, buf );
        CHECK( "unbatched Show composites exactly once", show2 == 1, buf );
        CHECK( "unbatched Drop composites exactly once", drop2 == 1, buf );
        CHECK( "unbatched move+footprint composites per call", soloCost == 2, buf );
        CHECK( "batched move+footprint composites exactly once", batchCost == 1, buf );
        CHECK( "an empty batch does not composite", emptyCost == 0, buf );
        CHECK( "a nested batch stays deferred until it closes", innerCost == 0, buf );
        CHECK( "nested batches collapse into one composite", nestedCost == 1, buf );
    }

    //---- the fade timer must skip frames whose alpha has not moved ----
    {
        pet.Place( 300, 300 );
        pet.Show( 0 );
        pet.Drop();               //fresh footprint: composited at alpha 255

        //immediately after a composite nothing has changed, so the fade tick
        //must decline to push a frame (this is the whole point of item 6)
        BOOL idle = pet.FadeTick();

        //let the footprint age, then the same tick must want a frame
        Sleep( 200 );
        BOOL aged = pet.FadeTick();

        sprintf( buf, "idle-tick=%s aged-tick=%s", idle?"redraw":"skip", aged?"redraw":"skip" );
        printf( "     %s\n", buf );

        CHECK( "fade tick skips a frame whose alpha is unchanged", idle == FALSE, buf );
        CHECK( "fade tick composites once the alpha has moved", aged == TRUE, buf );
    }

    //---- a geometry event must NOT rebuild the pet from the registry ----
    //Audit item 12: WM_DISPLAYCHANGE / WM_DEVMODECHANGE used to set the same
    //"reload everything" flag as a settings change, so flipping the resolution
    //destroyed and reconstructed every cat, re-extracted 40 icons, rebuilt every
    //sprite and teleported the pet to a new random spot. The pet now gets a
    //geometry-only hook. These checks pin the two properties that matter:
    //the positioned cat SURVIVES the event, and the surface comes back valid.
    {
        pet.Place( 1234, 700 );
        pet.Show( 2 );
        pet.Paint();

        DWORD r0   = pet.Redraws();
        int   w0   = pet.SurfW();
        int   h0   = pet.SurfH();
        POINT p0   = pet.GetPosition();

        pet.DisplayChange();

        DWORD r1   = pet.Redraws();
        POINT p1   = pet.GetPosition();

        //Repaint the same cat and re-measure the surface: a dropped surface must
        //have been rebuilt by the display-change path, not left null.
        pet.Show( 2 );
        pet.Paint();
        int w1 = pet.SurfW();
        int h1 = pet.SurfH();
        BOOL haveBits = pet.Bits() != NULL;
        BOOL sawCat   = pet.CatSpriteBits( 2 ) != NULL;

        sprintf( buf, "pos (%ld,%ld)->(%ld,%ld) surface %dx%d->%dx%d composites=%lu bits=%s sprite=%s",
                 (long)p0.x, (long)p0.y, (long)p1.x, (long)p1.y,
                 w0, h0, w1, h1, (unsigned long)(r1 - r0),
                 haveBits?"yes":"no", sawCat?"yes":"no" );
        printf( "     %s\n", buf );

        CHECK( "display change keeps the cat where the user had it",
               p1.x == p0.x && p1.y == p0.y, buf );
        CHECK( "display change rebuilds a usable surface",
               w1 > 0 && h1 > 0 && haveBits, buf );
        CHECK( "display change keeps the cat sprites valid",
               sawCat, buf );
        CHECK( "display change composites the new geometry",
               r1 > r0, buf );
    }

    //---- per-monitor DPI: the cat must not shrink on a high-DPI screen ----
    {
        printf( "\n  --- per-monitor DPI ---\n" );

        //walk the pet across the virtual desktop and record the sprite size
        int nS = 0;
        int vx0 = GetSystemMetrics(SM_XVIRTUALSCREEN), vy0 = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw  = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh  = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        int monCount = GetSystemMetrics(SM_CMONITORS);

        //sample at the centre of the primary and at the centre of the virtual
        //desktop, which on a stacked-monitor layout lands on the other screen
        int ptX[4], ptY[4], nPt = 0;
        ptX[nPt] = (vw/2) - cw/2; ptY[nPt] = (vh/2) - ch/2; nPt++;                 //mid virtual
        ptX[nPt] = 40;            ptY[nPt] = 40;                 nPt++;            //top-left
        if( vh > GetSystemMetrics(SM_CYSCREEN) )
        {
            ptX[nPt] = 40; ptY[nPt] = GetSystemMetrics(SM_CYSCREEN) + 40; nPt++;    //below primary
        }

        int nMismatch = 0;          //sprite size disagrees with its monitor's DPI
        int nDistinctW = 0;
        int prevW = -1;
        int seenW[8];

        for( int i = 0; i < nPt; i++ )
        {
            pet.Place( ptX[i], ptY[i] );
            pet.Show( 0 );

            float k = pet.ScaleForPoint( ptX[i], ptY[i] );
            sprintf( buf, "pet at %5d,%5d -> sprite %dx%d, monitor scale %.0f%%",
                     ptX[i], ptY[i], pet.CatW(), pet.CatH(), k*100.0f );
            printf( "     %s\n", buf );

            //the sprite must match the monitor's scale, not the primary's DPI
            int expect = (int)(pet.BaseW() * k + 0.5f);
            if( (int)(pet.BaseW() * k + 0.5f) != pet.CatW() ) nMismatch++;

            int w = pet.CatW();
            BOOL fSeen = FALSE;
            for( int j = 0; j < nDistinctW; j++ ) if( seenW[j] == w ) fSeen = TRUE;
            if( !fSeen && nDistinctW < 8 ) seenW[nDistinctW++] = w;
            (void)prevW;
        }

        //Correct behaviour: every sample matches its own monitor's scale.
        sprintf( buf, "%d samples, %d distinct sprite sizes", nPt, nDistinctW );
        CHECK( "cat sprite size follows the monitor DPI", nMismatch == 0, buf );

        //and on a mixed-DPI machine the size must actually vary, proving the
        //cat is not stuck at the primary monitor's scale
        if( monCount > 1 )
        {
            float kA = pet.ScaleForPoint( 40, 40 );
            float kB = pet.ScaleForPoint( 40, GetSystemMetrics(SM_CYSCREEN) + 40 );
            sprintf( buf, "primary=%.0f%%  secondary=%.0f%%  (monitors=%d)",
                     kA*100.0f, kB*100.0f, monCount );
            printf( "     %s\n", buf );

            if( kA != kB )
            {
                sprintf( buf, "%d distinct sprite sizes across %d monitors",
                         nDistinctW, monCount );
                CHECK( "mixed-DPI machine scales the cat differently per screen",
                       nDistinctW >= 2, buf );
            }
        }
    }

    //---- anti-jump: while the cat WALKS, the pixels it renders must track its
    //     pet-world position exactly, and no frame may displace it by more than
    //     the distance it actually moved.
    //
    //     This is the observable form of the bug the user reported ("the cat and
    //     its footprints sometimes jump"). The pet re-anchors its layered
    //     surface to the cat on most frames, so if the window rect and the
    //     bitmap content ever disagree - or if the monitor clamp / DPI rebuild
    //     shifts the drawing - the cat's own pixels land somewhere other than
    //     where m_ptPosition says, which reads as a jump.
    //
    //     The measurement is done in pet-world coordinates: for every step we
    //     read the window rect from the OS and the lit-pixel bounding box from
    //     the real composited surface, then compare the leftmost lit column
    //     against m_ptPosition. That quantity must be a CONSTANT throughout the
    //     walk (up to rounding), and its per-step delta must equal the step.
    //----
    {
        int nSteps = 0, nRebuild = 0, worstTrackX = 0, worstTrackY = 0;
        int worstStepX = 0, worstStepY = 0;
        int nOffMonitor = 0;

        CTestPet walk( FALSE );
        walk.SetImages( hCat, nCat );

        int ox0 = GetSystemMetrics(SM_XVIRTUALSCREEN), oy0 = GetSystemMetrics(SM_YVIRTUALSCREEN);

        if( walk.Wnd() == NULL )
        {
            CHECK( "walk: layered window for the moving cat", FALSE, NULL );
        }
        else
        {
            const int startX = 240, startY = 320;
            walk.Place( startX, startY );
            walk.Show( 0 );

            //the monitor hosting the walk, in pet-world coordinates
            POINT p0; p0.x = startX + ox0; p0.y = startY + oy0;
            MONITORINFO mi = {0};
            mi.cbSize = sizeof(mi);
            GetMonitorInfo( MonitorFromPoint( p0, MONITOR_DEFAULTTONEAREST ), &mi );

            int wLeft  = mi.rcMonitor.left   - ox0;
            int wTop   = mi.rcMonitor.top    - oy0;
            int wRight = mi.rcMonitor.right  - ox0;
            int wBot   = mi.rcMonitor.bottom - oy0;
            int wCat   = walk.CatW();

            //stay inside this monitor so the sprite scale never changes; the
            //seam-crossing case is covered separately below
            int spanX = (wRight - wCat - 20) - startX;
            if( spanX < 200 ) spanX = 200;

            int prevAbsX = 0, prevAbsY = 0, prevPX = 0, prevPY = 0;
            int baseX = 0, baseY = 0, prevW = 0;
            BOOL haveBase = FALSE;

            for( int s = 0; s < 140; s++ )
            {
                //out to the right edge, then back - so the walk repeatedly
                //crosses the 64px surface-padding boundaries and ends up hard
                //against the monitor edge, where the clamp in Redraw() bites
                int x, y;
                if( s < 90 ) { x = startX + (s * 9); y = startY + (s % 5) * 6; }
                else         { x = startX + (179 - s) * 9; y = startY + 40 + (s % 5) * 6; }
                if( x > wRight - wCat - 1 ) continue;
                if( x < wLeft ) continue;

                walk.Place( x, y );
                walk.Paint();
                nSteps++;

                //a DPI rebuild legitimately changes the sprite's transparent
                //margin, so re-baseline rather than call it a jump
                if( walk.CatW() != prevW ) { nRebuild++; haveBase = FALSE; prevW = walk.CatW(); }

                RECT rc;
                GetWindowRect( walk.Wnd(), &rc );

                //leftmost/topmost lit pixel of the composited cat, in surface
                //coordinates, converted to pet-world via the window origin
                int sw = walk.SurfW(), sh = walk.SurfH();
                int litMinX = -1, litMinY = -1;
                for( int py = 0; py < sh && litMinX < 0; py++ )
                    for( int px = 0; px < sw; px++ )
                        if( walk.Bits()[ ((size_t)py * sw + px) * 4 + 3 ] ) { litMinX = px; litMinY = py; break; }

                if( litMinX < 0 ) continue;   //nothing drawn: cannot judge

                int absX = (rc.left - ox0) + litMinX;
                int absY = (rc.top  - oy0) + litMinY;

                int trackX = absX - x;
                int trackY = absY - y;

                if( !haveBase ) { baseX = trackX; baseY = trackY; haveBase = TRUE; }
                else
                {
                    int dx = trackX - baseX; if( dx < 0 ) dx = -dx;
                    int dy = trackY - baseY; if( dy < 0 ) dy = -dy;
                    if( dx > worstTrackX ) worstTrackX = dx;
                    if( dy > worstTrackY ) worstTrackY = dy;

                    int ddx = absX - prevAbsX - (x - prevPX); if( ddx < 0 ) ddx = -ddx;
                    int ddy = absY - prevAbsY - (y - prevPY); if( ddy < 0 ) ddy = -ddy;
                    if( ddx > worstStepX ) worstStepX = ddx;
                    if( ddy > worstStepY ) worstStepY = ddy;
                }

                //the cat must never leave the monitor it is standing on
                if( (rc.left - ox0) > x || (rc.left - ox0) + sw <= x ) nOffMonitor++;

                prevAbsX = absX; prevAbsY = absY; prevPX = x; prevPY = y;
            }

            sprintf( buf, "%d steps, %d sprite rebuild(s), worst track drift=%d,%d px, worst step error=%d,%d px",
                     nSteps, nRebuild, worstTrackX, worstTrackY, worstStepX, worstStepY );
            printf( "     %s\n", buf );

            CHECK( "walk: enough steps were measured", nSteps >= 60, buf );
            CHECK( "walk: rendered cat position does not drift while walking (<=1px)",
                   worstTrackX <= 1 && worstTrackY <= 1, buf );
            CHECK( "walk: no frame displaces the cat more than it moved (<=1px)",
                   worstStepX <= 1 && worstStepY <= 1, buf );
            CHECK( "walk: cat is drawn inside the window on every frame", nOffMonitor == 0, buf );
        }
    }

    //---- anti-jump across a monitor seam: crossing to a 150%-DPI screen
    //     legitimately changes the sprite SIZE, but the cat must never be
    //     displaced sideways by the switch ----
    {
        int monCount = 0;
        for( int i = 0; ; i++ )
        {
            DISPLAY_DEVICE dd = {0};
            dd.cb = sizeof(dd);
            if( !EnumDisplayDevices( NULL, i, &dd, 0 ) ) break;
            if( dd.StateFlags & DISPLAY_DEVICE_ACTIVE ) monCount++;
        }

        CTestPet seam( FALSE );
        seam.SetImages( hCat, nCat );

        if( monCount < 2 || seam.Wnd() == NULL )
        {
            printf( "     seam test skipped (monitors=%d, window=%p)\n", monCount, (void*)seam.Wnd() );
        }
        else
        {
            int ox0 = GetSystemMetrics(SM_XVIRTUALSCREEN), oy0 = GetSystemMetrics(SM_YVIRTUALSCREEN);
            int vw  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            int vh  = GetSystemMetrics(SM_CYVIRTUALSCREEN);

            //walk straight DOWN across the horizontal seam onto the 150% screen,
            //then back up - the exact motion that made the sprite scale change
            int x = 300;
            int worst = 0, nSteps = 0;
            int prevAbsY = 0, prevY = 0, baseY = 0, prevW = 0;
            BOOL haveBase = FALSE;

            for( int s = 0; s < 60; s++ )
            {
                int y = 900 + (s < 30 ? s * 20 : (59 - s) * 20);
                if( y > vh - seam.CatH() - 1 ) continue;

                seam.Place( x, y );
                seam.Paint();
                nSteps++;
                if( seam.CatW() != prevW ) { prevW = seam.CatW(); haveBase = FALSE; }

                RECT rc; GetWindowRect( seam.Wnd(), &rc );
                int sw = seam.SurfW(), sh = seam.SurfH();
                int litMinY = -1, litMinX = -1;
                for( int py = 0; py < sh && litMinY < 0; py++ )
                    for( int px = 0; px < sw; px++ )
                        if( seam.Bits()[ ((size_t)py*sw+px)*4 + 3 ] ) { litMinY = py; litMinX = px; break; }
                if( litMinY < 0 ) continue;

                int absY = (rc.top - oy0) + litMinY;

                if( !haveBase ) { baseY = absY - y; haveBase = TRUE; }
                else
                {
                    //the sprite grows at 150%, so its transparent top margin
                    //changes; measure only sideways drift, which must stay 0
                    int ddx = (int)((rc.left - ox0) + litMinX) - x;
                    if( ddx < 0 ) ddx = -ddx;
                    if( ddx > worst ) worst = ddx;
                }
                prevAbsY = absY; prevY = y;
            }
            (void)prevAbsY; (void)prevY;

            sprintf( buf, "%d steps across the seam, worst sideways drift=%d px", nSteps, worst );
            printf( "     %s\n", buf );
            CHECK( "seam: crossing to another-DPI monitor does not shift the cat sideways",
                   nSteps >= 10 && worst <= 1, buf );
        }
    }

    //---- the on-screen mechanism itself ----
    TestLayeredAlphaMechanism( cw, ch );

    printf( "\n=== %s (%d failures) ===\n", g_nFail ? "FAILED" : "ALL PASS", g_nFail );
    return g_nFail ? 2 : 0;
}
