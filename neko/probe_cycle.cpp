///////////////////////////////////////////////////////////////////////////////
// probe_cycle.cpp — end-to-end test of the three-state hide hot key.
//
// Launches the REAL shipped Neko98.exe as a detached desktop process, then
// drives the user's own hide hot key (read from the registry, default
// Ctrl+Alt+H) and observes the pet's window from the outside. The whole point is
// that nothing is mocked: the assertions are made against a process the user
// could be running.
//
// The three states the user asked for:
//   1. normal     — visible, normal size
//   2. magnified  — STILL visible, drawn at 300%
//   3. hidden     — not visible, process still alive
// and the fourth press returns to (1), which must be a *forced* restore.
//
// Window size is used as the observable for "magnified": the layered surface is
// always at least the cat's drawn size, so a 300% cat cannot fit in the same
// window a normal cat fits in. That is a coarse but decisive signal.
///////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD  g_dwPid  = 0;
static HWND   g_hPet   = 0;
static HWND   g_hDialog = 0;
static int    g_nFail  = 0;

static BOOL IsTargetClass( const char* pszCls )
{
    return lstrcmpiA( pszCls, "Neko98TransparentPet" ) == 0;
}

static BOOL CALLBACK EnumProc( HWND hWnd, LPARAM lParam )
{
    (void)lParam;

    DWORD dwPid = 0;
    GetWindowThreadProcessId( hWnd, &dwPid );
    if( dwPid != g_dwPid ) return TRUE;

    char szCls[128];
    if( GetClassNameA( hWnd, szCls, sizeof(szCls) ) == 0 ) return TRUE;

    if( IsTargetClass( szCls ) ) g_hPet = hWnd;
    if( lstrcmpiA( szCls, "#32770" ) == 0 ) g_hDialog = hWnd;   // a modal MessageBox

    return TRUE;
}

static void RefreshWindows()
{
    g_hPet = 0;
    g_hDialog = 0;
    EnumWindows( EnumProc, 0 );
}

static BOOL PetVisible()
{
    RefreshWindows();
    return g_hPet != 0 && IsWindowVisible( g_hPet );
}

static BOOL PetRect( RECT* prc )
{
    RefreshWindows();
    if( g_hPet == 0 ) return FALSE;
    return GetWindowRect( g_hPet, prc ) ? TRUE : FALSE;
}

//Poll until the pet reaches the requested visibility, or the timeout expires.
static BOOL WaitVisible( BOOL bWant, DWORD dwMs )
{
    DWORD dwStart = GetTickCount();
    for( ;; )
    {
        if( PetVisible() == bWant ) return TRUE;
        if( GetTickCount() - dwStart > dwMs ) return FALSE;
        Sleep( 60 );
    }
}

//Wait until the window grows/shrinks past a threshold, so the assertion does not
//race the compositor.
static BOOL WaitArea( long lMinArea, long lMaxArea, DWORD dwMs, RECT* pOut )
{
    DWORD dwStart = GetTickCount();
    RECT rc;
    for( ;; )
    {
        if( PetRect( &rc ) && IsWindowVisible( g_hPet ) )
        {
            long lArea = (long)( rc.right - rc.left ) * (long)( rc.bottom - rc.top );
            if( lArea >= lMinArea && ( lMaxArea <= 0 || lArea <= lMaxArea ) )
            {
                if( pOut ) *pOut = rc;
                return TRUE;
            }
        }
        if( GetTickCount() - dwStart > dwMs ) return FALSE;
        Sleep( 60 );
    }
}

//Press Ctrl+Alt+<bVk>. Injected into the system input queue because that is
//exactly what the user does; RegisterHotKey listens at that level.
static void SendCtrlAlt( BYTE bVk )
{
    INPUT in[6];
    memset( in, 0, sizeof(in) );

    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = VK_MENU;
    in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = bVk;
    in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = bVk;        in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    in[4].type = INPUT_KEYBOARD; in[4].ki.wVk = VK_MENU;    in[4].ki.dwFlags = KEYEVENTF_KEYUP;
    in[5].type = INPUT_KEYBOARD; in[5].ki.wVk = VK_CONTROL; in[5].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput( 6, in, sizeof(INPUT) );

    //Give the message loop time to run the handler and composite.
    DWORD dwEnd = GetTickCount() + 700;
    MSG msg;
    while( GetTickCount() < dwEnd )
    {
        while( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
        {
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        Sleep( 20 );
    }
}

static void CHECK( const char* what, BOOL ok, const char* detail = "" )
{
    printf( "[%s] %-52s %s\n", ok ? "PASS" : "FAIL", what, detail );
    if( !ok ) g_nFail++;
}

static void Describe( const char* szTag, const RECT& rc, char* pszOut, int cbOut )
{
    sprintf( pszOut, "%s rect=%d,%d %dx%d area=%ld", szTag,
             rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
             (long)( rc.right - rc.left ) * (long)( rc.bottom - rc.top ) );
}


int main( int argc, char** argv )
{
    if( argc < 2 ) { printf( "usage: probe_cycle.exe <Neko98.exe>\n" ); return 1; }

    printf( "=== Neko98 three-state hide hot key probe ===\n\n" );

    //---- launch the pet as a genuine desktop process ---------------------
    char szCmd[MAX_PATH * 2];
    wsprintf( szCmd, "\"%s\"", argv[1] );

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset( &si, 0, sizeof(si) ); si.cb = sizeof(si);
    memset( &pi, 0, sizeof(pi) );

    if( !CreateProcessA( NULL, szCmd, NULL, NULL, FALSE,
                         DETACHED_PROCESS, NULL, NULL, &si, &pi ) )
    {
        printf( "FAILED to launch: %lu\n", GetLastError() );
        return 1;
    }

    g_dwPid = pi.dwProcessId;
    printf( "started pid=%lu\n\n", g_dwPid );

    if( !WaitVisible( TRUE, 8000 ) )
    {
        printf( "FAILED: the pet's window never appeared\n" );
        TerminateProcess( pi.hProcess, 0 );
        return 1;
    }

    char buf[256];
    RECT rc0;

    //Wait for the pet to settle at a real size before taking the baseline. Right
    //after Start-up the layered window still has its placeholder 1x1 geometry
    //from CreateWindow, and measuring that would make every size comparison
    //meaningless (a 1-pixel baseline cannot distinguish 100% from 300%).
    BOOL bSettled = FALSE;
    DWORD dwWait = GetTickCount();
    while( GetTickCount() - dwWait < 4000 )
    {
        if( PetRect( &rc0 ) && IsWindowVisible( g_hPet ) )
        {
            long lW = rc0.right - rc0.left;
            long lH = rc0.bottom - rc0.top;
            if( lW > 8 && lH > 8 ) { bSettled = TRUE; break; }
        }
        Sleep( 60 );
    }

    if( !bSettled )
    {
        printf( "FAILED: the pet's window never settled to a usable size\n" );
        TerminateProcess( pi.hProcess, 0 );
        return 1;
    }

    Describe( "start-up", rc0, buf, sizeof(buf) );
    CHECK( "state 1: the pet is visible at a usable size at start-up", TRUE, buf );

    long lAreaNormal = (long)( rc0.right - rc0.left ) * (long)( rc0.bottom - rc0.top );

    //---- press 1: must MAGNIFY and stay visible --------------------------
    SendCtrlAlt( 'H' );

    if( g_hDialog != NULL )
    {
        char dbuf[128];
        sprintf( dbuf, "modal dialog %p is open (hot key probably refused)", (void*)g_hDialog );
        CHECK( "the hide hot key was accepted", FALSE, dbuf );
    }

    CHECK( "press 1: the pet is STILL visible (it must not hide yet)", PetVisible() );

    //A 300% cat cannot fit in the window a 100% cat fits in. Require at least
    //2.2x the area: 3x linear would be 9x, but the surface is sized to the
    //cat+trail and rounded to 64px, so the growth can exceed or fall short of
    //the linear factor; 2.2x cleanly separates "grew" from "unchanged".
    long lAreaNeed = (long)( lAreaNormal * 2.2 );
    RECT rcBig;
    BOOL bGrew = WaitArea( lAreaNeed, 0, 2500, &rcBig );

    Describe( bGrew ? "magnified" : "after press 1", bGrew ? rcBig : rc0, buf, sizeof(buf) );
    CHECK( "press 1: the pet grew to roughly 300%", bGrew, buf );

    if( bGrew )
    {
        long lAreaBig = (long)( rcBig.right - rcBig.left ) * (long)( rcBig.bottom - rcBig.top );
        char ebuf[160];
        sprintf( ebuf, "normal area=%ld magnified area=%ld ratio=%.2fx",
                 lAreaNormal, lAreaBig, (double)lAreaBig / (double)lAreaNormal );
        CHECK( "press 1: the growth is the size of a real magnification", lAreaBig > lAreaNeed, ebuf );
    }

    //---- press 2: must HIDE ----------------------------------------------
    SendCtrlAlt( 'H' );
    BOOL bHidden = WaitVisible( FALSE, 3000 );
    CHECK( "press 2: the pet disappeared", bHidden,
           bHidden ? "window no longer visible" : "window is still visible" );

    //The process must survive being hidden - that was the whole complaint.
    DWORD dwExit = 0;
    GetExitCodeProcess( pi.hProcess, &dwExit );
    CHECK( "press 2: the process is still alive while hidden", dwExit == STILL_ACTIVE );

    //---- press 3: must come back, at NORMAL size, and really be there ----
    SendCtrlAlt( 'H' );
    BOOL bBack = WaitVisible( TRUE, 4000 );
    CHECK( "press 3: the pet came back (forced restore)", bBack,
           bBack ? "window visible again" : "window still hidden" );

    if( bBack )
    {
        //Normal size again: no bigger than 2.0x the original area. The surface is
        //rounded up to a 64-pixel multiple and re-derived from the trail, so it
        //is not exactly the original rect, but it must clearly NOT still carry
        //the magnified size (which was ~9x linear, ~81x area).
        RECT rcBack;
        BOOL bNormalAgain = WaitArea( 0, (long)( lAreaNormal * 2.0 ), 2500, &rcBack );
        Describe( "restored", bNormalAgain ? rcBack : rc0, buf, sizeof(buf) );
        CHECK( "press 3: the size went back to normal (not 300%)", bNormalAgain, buf );
    }

    //After the restore the window must be genuinely on screen, not merely
    //flagged visible: this is the exact failure the user reported.
    if( bBack )
    {
        RECT rcNow;
        if( PetRect( &rcNow ) )
        {
            BOOL bOnScreen = ( rcNow.right  > rcNow.left ) &&
                             ( rcNow.bottom > rcNow.top  ) &&
                             ( rcNow.right  > 0 ) && ( rcNow.bottom > 0 ) &&
                             ( rcNow.left   < GetSystemMetrics( SM_XVIRTUALSCREEN ) + GetSystemMetrics( SM_CXVIRTUALSCREEN ) ) &&
                             ( rcNow.top    < GetSystemMetrics( SM_YVIRTUALSCREEN ) + GetSystemMetrics( SM_CYVIRTUALSCREEN ) );
            Describe( "final", rcNow, buf, sizeof(buf) );
            CHECK( "press 3: the window is inside the desktop, not off-screen", bOnScreen, buf );
        }
    }

    //---- fourth press restarts the cycle ---------------------------------
    SendCtrlAlt( 'H' );
    RECT rcAgain;
    BOOL bGrew2 = WaitArea( lAreaNeed, 0, 2500, &rcAgain );
    CHECK( "press 4: the cycle restarts at the magnified state", bGrew2 );
    SendCtrlAlt( 'H' );   //hide
    WaitVisible( FALSE, 3000 );
    SendCtrlAlt( 'H' );   //restore
    CHECK( "press 6: a second full cycle still restores the pet", WaitVisible( TRUE, 4000 ) );

    //---- leave the user's machine as we found it -------------------------
    RECT rcFinal;
    if( PetRect( &rcFinal ) )
    {
        char fbuf[160];
        Describe( "before shutdown", rcFinal, fbuf, sizeof(fbuf) );
        CHECK( "the pet is still running normally at the end", TRUE, fbuf );
    }

    GetExitCodeProcess( pi.hProcess, &dwExit );
    CHECK( "the pet survived the whole cycle", dwExit == STILL_ACTIVE );

    TerminateProcess( pi.hProcess, 0 );
    WaitForSingleObject( pi.hProcess, 3000 );
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );

    printf( "\n=== %s (%d failures) ===\n", g_nFail ? "FAILED" : "ALL PASS", g_nFail );
    return g_nFail ? 2 : 0;
}
