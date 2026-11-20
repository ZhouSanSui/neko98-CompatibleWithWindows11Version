// probe_cloak.cpp — test the leading hypothesis for "process runs, cat not on screen".
//
// IsWindowVisible() can be TRUE while the window is genuinely not shown to the
// user.  Three independent mechanisms do this, and none of them is detectable
// with IsWindowVisible alone:
//
//   1. DWM cloaking (DWMWA_CLOAKED) — the window is composed nowhere.  Happens
//      when: the window lives on a DIFFERENT VIRTUAL DESKTOP, the window belongs
//      to another session, or DWM suspended it.  This is THE classic cause of
//      "the app is alive but I cannot see it" on Windows 10/11.
//   2. Session desktop — the window sits on a window station/desktop that is not
//      the one currently displayed.
//   3. Zero effective alpha on a layered window.
//
// Reports the cloak flags per window so the mechanism is identified, not guessed.
//
// Build: g++ -O2 -o probe_cloak.exe probe_cloak.cpp -lgdi32 -luser32 -ldwmapi
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD g_pid = 0;

// DWMWA_CLOAKED is 14; dwmapi.dll exposes it via DwmGetWindowAttribute.
typedef HRESULT (WINAPI *PFN_DGWA)(HWND, DWORD, PVOID, DWORD);
static PFN_DGWA g_pDwmGetWindowAttribute = NULL;

static const char* CloakText( DWORD dw )
{
    if( dw == 0 ) return "not cloaked";
    static char buf[256];
    buf[0] = 0;
    if( dw & 1 ) strcat( buf, "DWM_CLOAKED_APP " );      // cloaked by the app
    if( dw & 2 ) strcat( buf, "SHELL_CLOAKED " );        // cloaked by the shell (virtual desktop)
    if( dw & 4 ) strcat( buf, "INHERITED_CLOAK " );      // inherited from a parent
    if( dw & 8 ) strcat( buf, "CLOAKNOTIFY " );
    if( !buf[0] ) sprintf( buf, "unknown(0x%lX)", (unsigned long)dw );
    return buf;
}

static void DescribeDesktop( HWND hWnd, const char* szTag )
{
    DWORD dwSession = 0;
    DWORD dwPid = 0;
    GetWindowThreadProcessId( hWnd, &dwPid );
    ProcessIdToSessionId( dwPid, &dwSession );

    // The window's own desktop name via its thread.
    DWORD dwThread = GetWindowThreadProcessId( hWnd, NULL );
    char szName[256] = "(unknown)";
    HDESK hDesk = GetThreadDesktop( dwThread );
    if( hDesk )
    {
        DWORD dwNeeded = 0;
        if( GetUserObjectInformationA( hDesk, UOI_NAME, szName, sizeof szName - 1, &dwNeeded ) )
            szName[sizeof szName - 1] = 0;
        else
            lstrcpynA( szName, "(GetUserObjectInformation failed)", sizeof szName );
    }

    char szSta[256] = "(unknown)";
    HWINSTA hWin = GetProcessWindowStation();
    if( hWin )
    {
        DWORD dwNeeded = 0;
        GetUserObjectInformationA( hWin, UOI_NAME, szSta, sizeof szSta - 1, &dwNeeded );
        szSta[sizeof szSta - 1] = 0;
    }

    printf( "    %s session=%lu desktop=\"%s\" winsta=\"%s\"\n",
            szTag, (unsigned long)dwSession, szName, szSta );
}

static BOOL CALLBACK EnumProc( HWND hWnd, LPARAM )
{
    DWORD dwPid = 0;
    GetWindowThreadProcessId( hWnd, &dwPid );
    if( dwPid != g_pid ) return TRUE;

    char szClass[256] = {0};
    GetClassNameA( hWnd, szClass, sizeof szClass - 1 );
    if( strstr( szClass, "TransparentPet" ) == NULL ) return TRUE;

    printf( "  %s (hwnd=%p)\n", szClass, (void*)hWnd );
    printf( "    IsWindowVisible = %d\n", IsWindowVisible( hWnd ) );

    DWORD dwCloak = 0xFFFFFFFF;
    if( g_pDwmGetWindowAttribute )
    {
        HRESULT hr = g_pDwmGetWindowAttribute( hWnd, 14 /*DWMWA_CLOAKED*/,
                                                &dwCloak, sizeof dwCloak );
        if( SUCCEEDED( hr ) )
            printf( "    DWMWA_CLOAKED   = 0x%lX  -> %s\n",
                    (unsigned long)dwCloak, CloakText( dwCloak ) );
        else
            printf( "    DWMWA_CLOAKED   = query failed hr=0x%08lX\n", (unsigned long)hr );
    }
    else
    {
        printf( "    DWMWA_CLOAKED   = dwmapi unavailable\n" );
    }

    // DWMWA_EXTENDED_FRAME_BOUNDS (9): the rect DWM actually composes.
    RECT rcFrame = {0,0,0,0};
    if( g_pDwmGetWindowAttribute )
    {
        HRESULT hr = g_pDwmGetWindowAttribute( hWnd, 9, &rcFrame, sizeof rcFrame );
        if( SUCCEEDED( hr ) )
            printf( "    DWM frame bounds= %ld,%ld  %ldx%ld\n",
                    rcFrame.left, rcFrame.top,
                    rcFrame.right - rcFrame.left, rcFrame.bottom - rcFrame.top );
    }

    // Is the window's OWNER chain visible, and is it enabled?
    printf( "    enabled=%d owner=%p\n", IsWindowEnabled( hWnd ), (void*)GetWindow( hWnd, GW_OWNER ) );

    DescribeDesktop( hWnd, "window" );

    printf( "\n" );
    return TRUE;
}

int main( int argc, char** argv )
{
    if( argc < 2 ) { printf( "usage: probe_cloak.exe <pid>\n" ); return 2; }
    g_pid = (DWORD) atol( argv[1] );

    typedef BOOL (WINAPI *PFN_SET)(HANDLE);
    HMODULE hUser = LoadLibraryA( "user32.dll" );
    if( hUser )
    {
        PFN_SET pSet = (PFN_SET) GetProcAddress( hUser, "SetProcessDpiAwarenessContext" );
        if( pSet ) pSet( (HANDLE)(LONG_PTR)-4 );
    }

    HMODULE hDwm = LoadLibraryA( "dwmapi.dll" );
    if( hDwm ) g_pDwmGetWindowAttribute = (PFN_DGWA) GetProcAddress( hDwm, "DwmGetWindowAttribute" );

    printf( "=== cloak probe for pid %lu ===\n", (unsigned long)g_pid );

    // What desktop is THIS probe on?  If it differs from the pet's, we are
    // looking at the wrong virtual desktop ourselves.
    char szMySta[256] = "(?)";
    HWINSTA hMyWin = GetProcessWindowStation();
    if( hMyWin )
    {
        DWORD dwNeeded = 0;
        GetUserObjectInformationA( hMyWin, UOI_NAME, szMySta, sizeof szMySta - 1, &dwNeeded );
        szMySta[sizeof szMySta - 1] = 0;
    }
    char szMyDesk[256] = "(?)";
    HDESK hMyDesk = GetThreadDesktop( GetCurrentThreadId() );
    if( hMyDesk )
    {
        DWORD dwNeeded = 0;
        GetUserObjectInformationA( hMyDesk, UOI_NAME, szMyDesk, sizeof szMyDesk - 1, &dwNeeded );
        szMyDesk[sizeof szMyDesk - 1] = 0;
    }
    DWORD dwMySession = 0;
    ProcessIdToSessionId( GetCurrentProcessId(), &dwMySession );
    printf( "probe itself: session=%lu desktop=\"%s\" winsta=\"%s\"\n\n",
            (unsigned long)dwMySession, szMyDesk, szMySta );

    EnumWindows( EnumProc, 0 );
    return 0;
}
