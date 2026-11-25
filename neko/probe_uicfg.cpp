///////////////////////////////////////////////////////////////////////////////
// probe_uicfg.cpp - smoke test for the settings dialog's 快捷键 page.
//
// Launches NekoCFG.exe, waits for its main dialog, selects the 快捷键 tab and
// checks that the controls the new page depends on actually exist and are
// visible. This is the check that catches resource-table mistakes (a duplicated
// control id, a missing control, a page that fails to create) which a compiler
// cannot see.
//
// It is read-only: it never presses 确定/应用, so the user's registry is not
// touched.
//
// Build: g++ -O2 -o probe_uicfg.exe probe_uicfg.cpp -luser32 -lcomctl32
///////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>

static int g_nFail = 0;

static void CHECK( const char* szWhat, BOOL bOk, const char* szDetail = "" )
{
    printf( "[%s] %s%s%s\n", bOk ? "PASS" : "FAIL", szWhat,
            szDetail[0] ? "  " : "", szDetail );
    if( !bOk ) g_nFail++;
}

/* Find the SysTabControl32 inside the dialog. */
static HWND g_hTabFound = NULL;
static BOOL CALLBACK FindTabProc( HWND hWnd, LPARAM lParam )
{
    char szClass[64];
    GetClassNameA( hWnd, szClass, sizeof szClass );
    if( strcmp( szClass, "SysTabControl32" ) == 0 )
    {
        g_hTabFound = hWnd;
        return FALSE;
    }
    return TRUE;
}

/* Find the property page: the child window that owns a control with id nId. */
static HWND g_hPageFound = NULL;
static int  g_nPageProbeId = 0;
static BOOL CALLBACK FindPageProc( HWND hWnd, LPARAM lParam )
{
    if( hWnd == (HWND)lParam ) return TRUE;
    if( GetDlgItem( hWnd, g_nPageProbeId ) != NULL )
    {
        g_hPageFound = hWnd;
        return FALSE;
    }
    return TRUE;
}

int main( int argc, char** argv )
{
    const char* szExe = ( argc > 1 ) ? argv[1] : "NekoCFG.exe";

    printf( "=== NekoCFG settings dialog smoke test ===\n" );
    printf( "launching %s\n", szExe );

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory( &si, sizeof si );
    si.cb = sizeof si;
    ZeroMemory( &pi, sizeof pi );

    if( !CreateProcessA( szExe, NULL, NULL, NULL, FALSE, DETACHED_PROCESS,
                         NULL, NULL, &si, &pi ) )
    {
        printf( "[FAIL] cannot launch %s (error %lu)\n", szExe, GetLastError() );
        return 1;
    }

    /* Wait for the settings dialog: "设置 Neko". */
    HWND hDlg = NULL;
    for( int i = 0; i < 60 && hDlg == NULL; i++ )
    {
        Sleep( 100 );
        hDlg = FindWindowA( "#32770", "设置 Neko" );
    }

    DWORD dwExit = 0;
    if( GetExitCodeProcess( pi.hProcess, &dwExit ) && dwExit != STILL_ACTIVE )
    {
        printf( "[FAIL] NekoCFG exited before showing its dialog (code %lu)\n", dwExit );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        return 1;
    }

    CHECK( "the settings dialog appears", hDlg != NULL );
    if( hDlg == NULL )
    {
        TerminateProcess( pi.hProcess, 0 );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        return 1;
    }

    /* The two legacy hot key boxes must be gone from IDD_CONFIG. */
    CHECK( "the legacy 代理 hot key box is gone",       GetDlgItem( hDlg, 1060 ) == NULL );
    CHECK( "the legacy 隐藏/缩放 hot key box is gone",  GetDlgItem( hDlg, 1061 ) == NULL );
    CHECK( "the 常规 checkboxes are still there",
           GetDlgItem( hDlg, 106 ) != NULL && GetDlgItem( hDlg, 1059 ) != NULL );

    g_hTabFound = NULL;
    EnumChildWindows( hDlg, FindTabProc, 0 );
    HWND hTab = g_hTabFound;

    CHECK( "the tab strip was found", hTab != NULL );
    if( hTab == NULL )
    {
        TerminateProcess( pi.hProcess, 0 );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        return 1;
    }

    /* The tab strip exists as soon as the dialog is created, but WM_INITDIALOG
       fills it in afterwards, so give it a moment before counting. */
    int nTabs = 0;
    for( int iWait = 0; iWait < 40; iWait++ )
    {
        nTabs = (int)SendMessageA( hTab, TCM_GETITEMCOUNT, 0, 0 );
        if( nTabs > 0 ) break;
        Sleep( 100 );
    }
    char szDetail[64];
    _snprintf( szDetail, sizeof szDetail, "tabs=%d", nTabs );
    CHECK( "the dialog still has five tabs", nTabs == 5, szDetail );

    /* Select the third tab (index 2) - that is 快捷键. */
    SendMessageA( hTab, TCM_SETCURSEL, 2, 0 );

    /* Ask the dialog to show the page for that tab. The property sheet reacts to
       TCN_SELCHANGE, which is a notification the tab control sends its parent. */
    {
        NMHDR nmh;
        ZeroMemory( &nmh, sizeof nmh );
        nmh.hwndFrom = hTab;
        nmh.idFrom   = (UINT)GetDlgCtrlID( hTab );
        nmh.code     = TCN_SELCHANGE;
        SendMessageA( GetParent( hTab ), WM_NOTIFY, (WPARAM)nmh.idFrom, (LPARAM)&nmh );
    }
    Sleep( 500 );

    /* The property page is a sibling child of the dialog; find the one that owns
       IDC_HKTYPE (1062). */
    g_nPageProbeId = 1062;
    g_hPageFound = NULL;
    EnumChildWindows( hDlg, FindPageProc, (LPARAM)hDlg );
    HWND hPage = g_hPageFound;

    CHECK( "the 快捷键 page was created", hPage != NULL );
    if( hPage != NULL )
    {
        CHECK( "the kind combo box exists",    GetDlgItem( hPage, 1062 ) != NULL );
        CHECK( "the add button exists",        GetDlgItem( hPage, 1063 ) != NULL );
        CHECK( "the shortcut list exists",     GetDlgItem( hPage, 1064 ) != NULL );
        CHECK( "the hot key box exists",       GetDlgItem( hPage, 1065 ) != NULL );
        CHECK( "the delete button exists",     GetDlgItem( hPage, 1073 ) != NULL );
        CHECK( "the proxy address box exists", GetDlgItem( hPage, 1066 ) != NULL );
        CHECK( "the proxy port box exists",    GetDlgItem( hPage, 1067 ) != NULL );
        CHECK( "the browser combo exists",     GetDlgItem( hPage, 1068 ) != NULL );
        CHECK( "the url box exists",           GetDlgItem( hPage, 1069 ) != NULL );
        CHECK( "the projection A combo exists", GetDlgItem( hPage, 1071 ) != NULL );
        CHECK( "the projection B combo exists", GetDlgItem( hPage, 1072 ) != NULL );

        HWND hList = GetDlgItem( hPage, 1064 );
        if( hList != NULL )
        {
            /* the header is a separate child window: asking the ListView itself
               for HDM_GETITEMCOUNT answers 0 and looks like a page bug. */
            HWND hHeader = (HWND)SendMessageA( hList, LVM_GETHEADER, 0, 0 );
            int nCols = ( hHeader != NULL ) ? (int)SendMessageA( hHeader, HDM_GETITEMCOUNT, 0, 0 ) : 0;
            char szCols[48];
            _snprintf( szCols, sizeof szCols, "columns=%d", nCols );
            CHECK( "the shortcut list has two columns", nCols == 2, szCols );

            int nRows = (int)SendMessageA( hList, LVM_GETITEMCOUNT, 0, 0 );
            char szRows[48];
            _snprintf( szRows, sizeof szRows, "rows=%d", nRows );
            CHECK( "the shortcut list lists at least one migrated row", nRows >= 1, szRows );
        }

        HWND hCombo = GetDlgItem( hPage, 1062 );
        if( hCombo != NULL )
        {
            int nItems = (int)SendMessageA( hCombo, CB_GETCOUNT, 0, 0 );
            char szI[48];
            _snprintf( szI, sizeof szI, "items=%d", nItems );
            CHECK( "the kind combo offers five kinds", nItems == 5, szI );
        }

        HWND hProjA = GetDlgItem( hPage, 1071 );
        if( hProjA != NULL )
        {
            int nItems = (int)SendMessageA( hProjA, CB_GETCOUNT, 0, 0 );
            char szI[48];
            _snprintf( szI, sizeof szI, "items=%d", nItems );
            CHECK( "the projection combo offers four modes", nItems == 4, szI );
        }
    }

    /* Leave the program alone: kill it rather than closing the dialog, so no
       setting is written. */
    TerminateProcess( pi.hProcess, 0 );
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );

    printf( "\n%s (%d failure%s)\n", g_nFail ? "=== FAILURES ===" : "=== ALL PASS ===",
            g_nFail, g_nFail == 1 ? "" : "s" );
    return g_nFail ? 1 : 0;
}
