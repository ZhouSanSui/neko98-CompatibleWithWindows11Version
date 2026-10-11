// probe_hide.cpp - end-to-end check of the two global hotkeys against the REAL
// shipped Neko98.exe.
//
// It is an external observer: it links no production code, launches the pet,
// watches its layered window, injects the key combinations through SendInput and
// reports what actually happened on the desktop. That is the only way to prove
// the wiring (RegisterHotKey -> WM_HOTKEY -> toggle -> SetVisible) works as a
// whole rather than only in unit tests.
//
// usage: probe_hide.exe <path-to-Neko98.exe>
//
// The caller is responsible for saving/restoring the Paused/Hidden registry
// values, because toggling really does persist them.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD g_dwPid      = 0;
static HWND  g_hPet       = NULL;
static HWND  g_hDialog    = NULL;

static BOOL IsTargetClass( const char* pszCls )
{
    return ( strcmp( pszCls, "Neko98TransparentPet" ) == 0 );
}

static BOOL CALLBACK EnumProc( HWND hWnd, LPARAM lParam )
{
    (void)lParam;

    DWORD dwPid = 0;
    GetWindowThreadProcessId( hWnd, &dwPid );
    if( dwPid != g_dwPid ) return TRUE;

    char szCls[128];
    szCls[0] = '\0';
    GetClassNameA( hWnd, szCls, sizeof(szCls) - 1 );

    if( IsTargetClass( szCls ) ) g_hPet = hWnd;

    //A modal dialog would mean the hotkey registration failed and the pet is
    //stuck behind a message box; report it rather than looping forever.
    if( strcmp( szCls, "#32770" ) == 0 && IsWindowVisible( hWnd ) ) g_hDialog = hWnd;

    return TRUE;
}

/* Re-scan and return the pet window (NULL when it has gone away). */
static HWND FindPetWindow()
{
    g_hPet = NULL;
    g_hDialog = NULL;
    EnumWindows( EnumProc, 0 );
    return g_hPet;
}

static BOOL PetVisible()
{
    HWND h = FindPetWindow();
    return ( h != NULL && IsWindowVisible( h ) != FALSE );
}

/* Wait up to dwMs for the pet window to reach the wanted visibility. */
static BOOL WaitVisible( BOOL bWant, DWORD dwMs )
{
    DWORD dwStart = GetTickCount();
    for( ;; )
    {
        if( PetVisible() == bWant ) return TRUE;
        if( GetTickCount() - dwStart > dwMs ) return FALSE;
        Sleep( 100 );
    }
}

/* Inject Ctrl+Alt+<vk>. */
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

    //Give the pet's message loop time to act.
    Sleep( 700 );
}

static int g_nFail = 0;
static void CHECK( const char* what, BOOL ok, const char* detail )
{
    printf( "[%s] %-50s %s\n", ok ? "PASS" : "FAIL", what, detail ? detail : "" );
    if( !ok ) g_nFail++;
}

int main( int argc, char** argv )
{
    if( argc < 2 ) { printf( "usage: probe_hide.exe <Neko98.exe>\n" ); return 1; }

    printf( "=== Neko98 hide/show hotkey probe ===\n" );

    //---- launch the pet, detached, so it is a genuine desktop process ----
    char szCmd[MAX_PATH * 2];
    wsprintf( szCmd, "\"%s\"", argv[1] );

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset( &si, 0, sizeof(si) );
    si.cb = sizeof(si);
    memset( &pi, 0, sizeof(pi) );

    if( !CreateProcessA( NULL, szCmd, NULL, NULL, FALSE,
                         DETACHED_PROCESS, NULL, NULL, &si, &pi ) )
    {
        printf( "FAILED to launch: %lu\n", GetLastError() );
        return 1;
    }

    g_dwPid = pi.dwProcessId;
    printf( "started pid=%lu\n\n", g_dwPid );

    //---- wait for the layered window ----
    if( !WaitVisible( TRUE, 6000 ) )
    {
        printf( "FAILED: the pet's window never appeared\n" );
        TerminateProcess( pi.hProcess, 0 );
        return 1;
    }

    char buf[256];
    RECT rc;
    GetWindowRect( g_hPet, &rc );
    sprintf( buf, "rect=%d,%d %dx%d", rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top );
    CHECK( "pet window is visible at start-up", TRUE, buf );

    //---- Ctrl+Alt+H must HIDE ----
    SendCtrlAlt( 'H' );
    BOOL bHidden = WaitVisible( FALSE, 3000 );

    if( g_hDialog != NULL )
    {
        printf( "\nNOTE: a modal dialog (%p) is open - the hot key was probably\n"
                "      refused because another program owns it.\n", (void*)g_hDialog );
    }

    CHECK( "Ctrl+Alt+H hides the cat", bHidden, bHidden ? "window no longer visible" : "still visible" );

    //---- and must SHOW it again ----
    SendCtrlAlt( 'H' );
    BOOL bBack = WaitVisible( TRUE, 3000 );
    CHECK( "Ctrl+Alt+H again shows the cat", bBack, bBack ? "window visible again" : "still hidden" );

    //---- Ctrl+Alt+N is PAUSE, which must NOT hide the pet ----
    SendCtrlAlt( 'N' );
    BOOL bStillThere = PetVisible();
    CHECK( "Ctrl+Alt+N pauses without hiding", bStillThere, bStillThere ? "window still visible" : "unexpectedly hidden" );

    //---- unpause, just to leave a clean state ----
    SendCtrlAlt( 'N' );
    CHECK( "the cat is still there afterwards", PetVisible(), NULL );

    //---- the process must still be alive and responsive ----
    DWORD dwCode = 0;
    GetExitCodeProcess( pi.hProcess, &dwCode );
    sprintf( buf, "exit code %lu", dwCode );
    CHECK( "pet process is still running", dwCode == STILL_ACTIVE, buf );

    TerminateProcess( pi.hProcess, 0 );
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );
    printf( "\nprobe terminated the pet\n" );

    printf( "\n=== %s (%d failures) ===\n", g_nFail ? "FAILED" : "PROBE PASSED", g_nFail );
    return g_nFail ? 2 : 0;
}
