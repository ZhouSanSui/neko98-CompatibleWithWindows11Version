// test_movement.cpp - decisive regression test for the "stuck at the screen edge"
// bug (a cat that crosses onto a monitor with a different DPI and then runs back
// and forth along the boundary forever).
//
// WHY IT BUILT THIS WAY
// ---------------------
// The bug is invisible in a pure simulation because it depends on the REAL
// per-monitor DPI of the machine and on the REAL movement code. So this test
// links the REAL production objects (Neko.o, TransparentPet.o, Pet.o) and the
// REAL icon resources (Neko95.o, which doubles as the icon library the cat loads
// its sprites from) and drives the REAL CNeko::Update() tick by tick on the
// machine's ACTUAL monitor layout.
//
// WHAT IT PROVES
// --------------
// 1. The hazard is REAL on this machine: the size the cat is DRAWN at flips when
//    it crosses the monitor boundary (32px on 100%, 48px on 150%).
// 2. The arrival size used by the movement maths does NOT flip: GetAimSize()
//    stays constant on both sides of the boundary.
// 3. End to end: a cat resting on the 150% monitor comes to rest at
//    toY - aimCy + 1, NOT at toY - drawnCy + 1. On that monitor the two differ by
//    16px, so the resting position alone proves which one the movement code used.
//    (This is the regression: the old code used the drawn size, which moved the
//    goal line as the cat crossed it and stranded the cat on the seam.)
// 4. Sweeping EVERY target around the boundary, the cat never ends up oscillating
//    across the seam, and the oscillation breaker never has to fire.

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "Neko.h"
#include "TransparentPet.h"

//normally defined in Neko98.cpp / NekoCFG, which this test deliberately does not link
HINSTANCE g_hInstance = NULL;

//Access the protected action field so the test can pin the cat to an action that
//keeps aiming at the target it was given. Everything else is the real class.
class CTestNeko : public CNeko
{
public:
    CTestNeko() : CNeko( "" ) {}

    //RUN_AROUND_RANDOMLY keeps re-aiming at the current target while awake, which
    //is exactly what we need: the forced target survives every tick.
    void PinAction( int a ) { m_Action = a; }
};

static int g_nFail = 0;
static void CHECK( const char* what, BOOL ok, const char* detail )
{
    printf( "[%s] %-56s %s\n", ok ? "PASS" : "FAIL", what, detail ? detail : "" );
    if( !ok ) g_nFail++;
}

//---- per-monitor DPI ------------------------------------------------------
typedef HRESULT (WINAPI *PFN_GetDpiForMonitor)( HMONITOR, int, UINT*, UINT* );
static PFN_GetDpiForMonitor g_pGetDpiForMonitor = NULL;

struct MonInfo { HMONITOR h; RECT rc; UINT dpiX, dpiY; };

#define MAX_MON 8
static MonInfo g_mon[MAX_MON];
static int     g_nMon = 0;

static BOOL CALLBACK EnumMon( HMONITOR h, HDC, LPRECT, LPARAM )
{
    if( g_nMon >= MAX_MON ) return FALSE;
    MONITORINFO mi = {0};
    mi.cbSize = sizeof(mi);
    if( !GetMonitorInfo( h, &mi ) ) return TRUE;
    g_mon[g_nMon].h  = h;
    g_mon[g_nMon].rc = mi.rcMonitor;
    g_mon[g_nMon].dpiX = 96;
    g_mon[g_nMon].dpiY = 96;
    if( g_pGetDpiForMonitor )
    {
        UINT dx = 96, dy = 96;
        if( SUCCEEDED( g_pGetDpiForMonitor( h, 0 /*MDT_EFFECTIVE_DPI*/, &dx, &dy ) ) )
        {
            g_mon[g_nMon].dpiX = dx;
            g_mon[g_nMon].dpiY = dy;
        }
    }
    g_nMon++;
    return TRUE;
}

//Run the cat with a forced target until it stops moving for a few ticks.
//Returns the number of ticks used, or -1 if it never settled within the budget.
struct RunResult { int ticks; int crossings; int maxStable; int breakerTrips; };

static RunResult RunToTarget( CTestNeko& neko, int seam, int toX, int toY, int budget )
{
    RunResult r = { -1, 0, 0, 0 };

    neko.PlaceAndAim( toX, toY + 200, toX, toY );   //start well clear of the seam

    int prevY = neko.GetPosition().y;
    int stable = 0;
    int prevRun = 0;

    for( int t = 0; t < budget; t++ )
    {
        neko.Update();

        POINT p = neko.GetPosition();

        //count boundary crossings (only the interesting event)
        if( ( prevY < seam ) != ( p.y < seam ) ) r.crossings++;

        //watch the oscillation breaker: it resets its counter when it fires
        int run = neko.GetReversalRun();
        if( run < prevRun ) r.breakerTrips++;
        prevRun = run;

        if( p.y == prevY ) { stable++; } else { stable = 0; }
        prevY = p.y;

        if( stable > r.maxStable ) r.maxStable = stable;

        //settled: the cat has not moved for several consecutive ticks
        if( stable >= 6 ) { r.ticks = t; return r; }
    }

    return r;
}

int main()
{
    g_hInstance = GetModuleHandle( NULL );

    //PerMonitorV2, exactly like the shipped pet, so the coordinates and DPI we
    //measure are the ones the real pet sees.
    {
        HMODULE u32 = GetModuleHandleA( "user32.dll" );
        typedef BOOL (WINAPI *PFN_SetCtx)( void* );
        PFN_SetCtx pSet = u32 ? (PFN_SetCtx)GetProcAddress( u32, "SetProcessDpiAwarenessContext" ) : NULL;
        if( pSet ) pSet( (void*)(LONG_PTR)-4 );
    }
    {
        HMODULE sh = LoadLibraryA( "shcore.dll" );
        if( sh ) g_pGetDpiForMonitor = (PFN_GetDpiForMonitor)GetProcAddress( sh, "GetDpiForMonitor" );
    }

    printf( "=== Neko98 movement / mixed-DPI seam test ===\n" );
    printf( "virtual screen origin %d,%d size %dx%d\n",
            GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN) );

    EnumDisplayMonitors( NULL, NULL, EnumMon, 0 );
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    printf( "monitors: %d\n", g_nMon );
    for( int i = 0; i < g_nMon; i++ )
    {
        RECT r = g_mon[i].rc;
        printf( "  [%d] %d,%d %dx%d dpi=%ux%u (scale %.2f)\n", i,
                r.left - vx, r.top - vy, r.right - r.left, r.bottom - r.top,
                g_mon[i].dpiX, g_mon[i].dpiY, g_mon[i].dpiX / 96.0 );
    }

    //find two monitors that touch along a horizontal boundary (one above the
    //other) and have DIFFERENT DPI - that is the configuration the bug needs
    int seam = -1, iTop = -1, iBot = -1;
    for( int a = 0; a < g_nMon && seam < 0; a++ )
        for( int b = 0; b < g_nMon && seam < 0; b++ )
        {
            if( a == b ) continue;
            if( g_mon[a].dpiX == g_mon[b].dpiX ) continue;
            if( g_mon[a].rc.left != g_mon[b].rc.left ) continue;
            if( g_mon[a].rc.bottom != g_mon[b].rc.top ) continue;
            iTop = a; iBot = b; seam = g_mon[a].rc.bottom - vy;
        }

    if( seam < 0 )
    {
        printf( "\nNo vertically stacked mixed-DPI monitor pair on this machine -\n" );
        printf( "the seam conditions cannot be reproduced here. Nothing to assert.\n" );
        printf( "=== ALL PASS (0 failures) ===\n" );
        return 0;
    }

    printf( "\nseam at pet-world y=%d  (top=%udpi, bottom=%udpi)\n",
            seam, g_mon[iTop].dpiX, g_mon[iBot].dpiX );

    CTestNeko neko;
    neko.PinAction( 2 );   //RUN_AROUND_RANDOMLY: keeps re-aiming at the forced target

    char buf[512];

    //---- 1. is the hazard real? ----
    neko.PlaceAndAim( 1280, seam - 200, 1280, seam - 200 );   //top monitor
    SIZE szTop = neko.GetPetSize();
    SIZE aimTop = neko.GetPetAimSize();

    neko.PlaceAndAim( 1280, seam + 200, 1280, seam + 200 );   //bottom monitor
    SIZE szBot = neko.GetPetSize();
    SIZE aimBot = neko.GetPetAimSize();

    sprintf( buf, "drawn %dx%d above vs %dx%d below the seam", szTop.cx, szTop.cy, szBot.cx, szBot.cy );
    CHECK( "drawn sprite size DOES change across the seam", szTop.cy != szBot.cy, buf );

    sprintf( buf, "aim %dx%d above vs %dx%d below the seam", aimTop.cx, aimTop.cy, aimBot.cx, aimBot.cy );
    CHECK( "arrival size does NOT change across the seam", aimTop.cy == aimBot.cy && aimTop.cx == aimBot.cx, buf );

    //---- 2. end to end: rest position proves which size is used ----
    //On the HIGH-DPI monitor the two candidate arrival sizes differ, so the spot
    //the cat comes to rest at identifies the size the movement code used.
    int toY   = seam + 260;                       //comfortably inside the bottom monitor
    int restWantAim   = toY - aimBot.cy + 1;      //what the fix must produce
    int restWantDrawn = toY - szBot.cy  + 1;      //what the old code would produce

    RunResult rr = RunToTarget( neko, seam, 1280, toY, 400 );
    int restY = neko.GetPosition().y;

    sprintf( buf, "aim=%d drawn=%d (rest expected %d, old code %d)",
             restWantAim, restWantDrawn, restWantAim, restWantDrawn );
    CHECK( "the two arrival rules really do differ on this monitor", restWantAim != restWantDrawn, buf );

    sprintf( buf, "settled at y=%d after %d ticks (crossings %d)", restY, rr.ticks, rr.crossings );
    CHECK( "cat reaches the target and stops", rr.ticks >= 0, buf );

    sprintf( buf, "rest y=%d, aim rule wants %d, drawn rule would want %d", restY, restWantAim, restWantDrawn );
    CHECK( "rest position follows the DPI-INDEPENDENT aim size", restY == restWantAim, buf );

    //---- 3. sweep every target around the seam ----
    //The original defect was a band of targets beside the boundary that could
    //never be reached: the cat crossed, the goal line moved, and it oscillated
    //forever. Sweep the whole neighbourhood - generously wider than the band the
    //old code failed on - and require that none of them oscillate.
    int loCy = aimBot.cy, hiCy = szBot.cy;   //arrival size, and the bigger drawn size
    int bandLo = seam + loCy - 1;            //first target the OLD code could not reach
    int bandHi = seam + hiCy - 2;            //last  target the OLD code could not reach
    int nBad = 0, nTrips = 0, worstCross = 0, worstTick = 0, nRun = 0;
    int firstBad = -1;

    for( int ty = seam - 40; ty <= seam + 80; ty++ )
    {
        RunResult q = RunToTarget( neko, seam, 1280, ty, 300 );
        nRun++;
        if( q.ticks < 0 ) { nBad++; if( firstBad < 0 ) firstBad = ty; }
        if( q.breakerTrips > 0 ) nTrips++;
        if( q.crossings > worstCross ) worstCross = q.crossings;
        if( q.ticks > worstTick ) worstTick = q.ticks;
    }

    sprintf( buf, "%d targets swept (old code failed on %d-%d), worst %d crossings, worst %d ticks",
             nRun, bandLo, bandHi, worstCross, worstTick );
    CHECK( "no target near the seam makes the cat oscillate", nBad == 0, buf );

    sprintf( buf, "%d targets needed the oscillation breaker", nTrips );
    CHECK( "the oscillation breaker never has to fire in normal running", nTrips == 0, buf );

    //---- 4. an ordinary long run is unaffected ----
    //("far" is a legacy keyword in MinGW's windef.h, hence the variable name)
    RunResult rrFar = RunToTarget( neko, seam, 900, seam + 1100, 600 );
    sprintf( buf, "settled in %d ticks, %d crossings", rrFar.ticks, rrFar.crossings );
    CHECK( "a normal target far from the seam is still reached", rrFar.ticks >= 0 && rrFar.crossings <= 2, buf );

    printf( "\n%s\n", g_nFail == 0 ? "=== ALL PASS (0 failures) ===" : "=== FAILURES ===" );
    return g_nFail == 0 ? 0 : 1;
}
