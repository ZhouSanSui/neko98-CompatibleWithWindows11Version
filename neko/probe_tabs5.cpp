// probe_tabs5.cpp - judge the tab switch by what the user actually SEES.
//
// Two independent notions of "which page is up":
//   * logical : IsWindowVisible(page)  -> only the WS_VISIBLE bit
//   * rendered: WindowFromPoint(x,y)   -> the window that actually paints there
//
// A page can be logically visible and still be painted somewhere else entirely,
// or be covered by a sibling, and then the user sees the wrong thing while the
// tab control already highlights the new tab. This probe samples ONE FIXED
// rectangle - the tab body - and takes a majority vote over 9 points, so it
// reports the page that is really painted in the tab area.
//
// Sampling per page's own rectangle would be useless: a page that was never
// repositioned sits at the top left of the dialog, so sampling its own rectangle
// finds it there and "confirms" it even though the tab body shows something
// else. That blind spot is exactly how the reported bug could hide.
//
// Clicks are posted (WM_LBUTTONDOWN/UP carry only client coordinates), so the
// dialog does not have to be in the foreground.
//
// usage: probe_tabs5.exe <NekoCFG.exe> [rounds]

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct tagKIDS { int aId[96]; int n; } KIDS;

static int g_nPass = 0;
static int g_nFail = 0;

static void Check( const char* szWhat, BOOL bOk, const char* szDetail )
{
    if( bOk ) { g_nPass++; printf( "[PASS] %s", szWhat ); }
    else      { g_nFail++; printf( "[FAIL] %s", szWhat ); }
    if( szDetail != NULL ) printf( "  %s", szDetail );
    printf( "\n" );
    fflush( stdout );
}

static BOOL CALLBACK CollectKids( HWND hWnd, LPARAM lParam )
{
    KIDS* lp = (KIDS*)lParam;
    int nId = GetDlgCtrlID( hWnd );
    if( nId > 0 && lp->n < 96 ) lp->aId[lp->n++] = nId;
    return TRUE;
}

static BOOL Has( const KIDS* lp, int nId )
{
    int i;
    for( i = 0; i < lp->n; i++ ) if( lp->aId[i] == nId ) return TRUE;
    return FALSE;
}

/* Each page is identified by a control id that only it owns. */
static int PageIndexOf( HWND hPage )
{
    KIDS k;
    k.n = 0;
    EnumChildWindows( hPage, CollectKids, (LPARAM)&k );
    if( Has( &k, 1062 ) ) return 2;   /* IDC_HKTYPE - 快捷键 */
    if( Has( &k, 1036 ) ) return 0;   /* 显示 */
    if( Has( &k, 1054 ) ) return 3;   /* 独立行为 */
    if( Has( &k, 1058 ) ) return 4;   /* 特效 */
    if( Has( &k, 102 )  ) return 1;   /* 移动 */
    return -1;
}

static HWND g_hDlg;
static HWND g_hTab;
static HWND g_aPageSnap[8];
static int  g_nPageSnap;

static BOOL CALLBACK CollectPageProc( HWND hWnd, LPARAM lParam )
{
    (void)lParam;
    if( PageIndexOf( hWnd ) >= 0 && g_nPageSnap < 8 ) g_aPageSnap[g_nPageSnap++] = hWnd;
    return TRUE;
}

static void CollectPages( HWND hDlg, HWND* pOut, int* pCount )
{
    int i;
    g_nPageSnap = 0;
    EnumChildWindows( hDlg, CollectPageProc, 0 );
    for( i = 0; i < g_nPageSnap && i < 8; i++ ) pOut[i] = g_aPageSnap[i];
    *pCount = ( g_nPageSnap < 8 ) ? g_nPageSnap : 8;
}

/* Which page is actually painted in the tab body?
 *
 * The sample rectangle is DERIVED FROM THE TAB CONTROL, not from the pages:
 * the body starts just below the label band (the page rects sit 42 px below the
 * tab control's top edge) and spans the tab control's client area. Deriving it
 * from the pages would be circular - a page that was never repositioned sits at
 * the dialog's top left, and sampling around it would only ever report that
 * same page, which is precisely how the reported bug stayed invisible.
 */
static int SeenPageIndex( void )
{
    static const int ax[3] = { 1, 2, 3 };
    static const int ay[3] = { 1, 2, 3 };
    int  nVote[8];
    int  i, j, k, nBest, nTop;
    RECT rcTab, rcBody;

    if( g_hTab == NULL ) return -1;

    GetWindowRect( g_hTab, &rcTab );
    rcBody = rcTab;
    rcBody.top += 42;   /* label band height, matches g_rcTabStart.top += 42 */
    rcBody.left += 3;   /* matches g_rcTabStart.left += 3 */
    if( rcBody.bottom - rcBody.top < 20 || rcBody.right - rcBody.left < 20 )
        return -1;

    for( i = 0; i < 8; i++ ) nVote[i] = 0;

    for( j = 0; j < 3; j++ )
    {
        for( k = 0; k < 3; k++ )
        {
            POINT pt;
            HWND  hHit;
            int   nGuard = 0;
            pt.x = rcBody.left + ( rcBody.right  - rcBody.left ) * ax[j] / 4;
            pt.y = rcBody.top  + ( rcBody.bottom - rcBody.top  ) * ay[k] / 4;
            hHit = WindowFromPoint( pt );
            while( hHit != NULL && hHit != g_hDlg && nGuard < 16 )
            {
                int n = PageIndexOf( hHit );
                if( n >= 0 ) { nVote[n]++; break; }
                hHit = GetParent( hHit );
                nGuard++;
            }
        }
    }

    nBest = -1; nTop = 0;
    for( i = 0; i < 8; i++ ) if( nVote[i] > nTop ) { nTop = nVote[i]; nBest = i; }
    return ( nTop > 0 ) ? nBest : -1;
}

typedef struct tagFIND { DWORD dwPid; HWND hFound; HWND hTab; } FIND;

static BOOL CALLBACK FindMainProc( HWND hWnd, LPARAM lParam )
{
    FIND* lp = (FIND*)lParam;
    DWORD dwPid = 0;
    char  szClass[64];
    GetWindowThreadProcessId( hWnd, &dwPid );
    if( dwPid != lp->dwPid ) return TRUE;
    GetClassNameA( hWnd, szClass, sizeof szClass );
    if( strcmp( szClass, "#32770" ) == 0 ) { lp->hFound = hWnd; return FALSE; }
    return TRUE;
}

static BOOL CALLBACK FindTabProc( HWND hWnd, LPARAM lParam )
{
    FIND* lp = (FIND*)lParam;
    char szClass[64];
    GetClassNameA( hWnd, szClass, sizeof szClass );
    if( strcmp( szClass, "SysTabControl32" ) == 0 ) { lp->hTab = hWnd; return FALSE; }
    return TRUE;
}

static void ClickClient( HWND hTab, int x, int y )
{
    LPARAM lp = MAKELPARAM( x, y );
    PostMessageA( hTab, WM_LBUTTONDOWN, MK_LBUTTON, lp );
    Sleep( 60 );
    PostMessageA( hTab, WM_LBUTTONUP, 0, lp );
    Sleep( 400 );
}

/* Walk the label band and click until the wanted index really becomes current. */
static BOOL ClickTabIndex( HWND hTab, int nWant )
{
    int y, x;
    for( y = 8; y <= 40; y += 4 )
    {
        for( x = 6; x <= 274; x += 4 )
        {
            int nAfter;
            ClickClient( hTab, x, y );
            nAfter = (int)SendMessageA( hTab, TCM_GETCURSEL, 0, 0 );
            if( nAfter == nWant ) return TRUE;
        }
    }
    return FALSE;
}

int main( int argc, char** argv )
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char szCmd[MAX_PATH + 8];
    FIND fm;
    int  nRounds = 1, nRound, i, nWait;

    printf( "=== probe_tabs5 ===\n" );
    fflush( stdout );

    if( argc < 2 ) { printf( "usage: probe_tabs5.exe <NekoCFG.exe> [rounds]\n" ); return 2; }
    if( argc >= 3 ) nRounds = atoi( argv[2] );
    if( nRounds < 1 ) nRounds = 1;

    ZeroMemory( &si, sizeof si ); si.cb = sizeof si;
    ZeroMemory( &pi, sizeof pi );
    _snprintf( szCmd, sizeof szCmd, "\"%s\"", argv[1] );
    if( !CreateProcessA( NULL, szCmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ) )
    {
        printf( "could not start NekoCFG (%lu)\n", GetLastError() );
        return 2;
    }

    fm.dwPid = pi.dwProcessId; fm.hFound = NULL; fm.hTab = NULL;
    for( nWait = 0; nWait < 120 && fm.hFound == NULL; nWait++ )
    {
        EnumWindows( FindMainProc, (LPARAM)&fm );
        if( fm.hFound == NULL ) Sleep( 100 );
    }
    g_hDlg = fm.hFound;
    if( g_hDlg == NULL )
    {
        printf( "the settings dialog did not appear\n" );
        TerminateProcess( pi.hProcess, 0 );
        return 1;
    }
    Check( "the settings dialog appeared", TRUE, "" );

    for( nWait = 0; nWait < 60 && fm.hTab == NULL; nWait++ )
    {
        EnumChildWindows( g_hDlg, FindTabProc, (LPARAM)&fm );
        /* Raise the dialog so another window cannot cover the tab body while we
       sample it with WindowFromPoint(). Without this a stray window over the
       dialog makes every sample miss and reports seen=-1, which looks like a
       product failure but is only an occlusion artefact. */
    SetWindowPos( g_hDlg, HWND_TOPMOST, 0, 0, 0, 0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
    Sleep( 400 );

    g_hTab = fm.hTab;
    if( fm.hTab == NULL ) Sleep( 100 );
    }
    if( fm.hTab == NULL )
    {
        printf( "no tab control\n" );
        TerminateProcess( pi.hProcess, 0 );
        return 1;
    }

    /* A second InitialisePropertyDialog() would append another five items and
       every visual mapping would be wrong even though the logic looks fine. */
    {
        int nItems = (int)SendMessageA( fm.hTab, TCM_GETITEMCOUNT, 0, 0 );
        char szDetail[64];
        _snprintf( szDetail, sizeof szDetail, "items=%d", nItems );
        Check( "the tab control holds exactly five items", nItems == 5, szDetail );
    }

    /* Report where the pages actually are: all of them must share the tab body.
       A page left at the dialog's top left is the bug this probe exists for. */
    {
        HWND hPages[8];
        int  nPages = 0, k;
        RECT rcTab;
        int  nAtBodyTop = 0;
        char szDetail[512];

        CollectPages( g_hDlg, hPages, &nPages );
        GetWindowRect( fm.hTab, &rcTab );

        printf( "  tab rect: %ld,%ld %ldx%ld\n",
                (long)rcTab.left, (long)rcTab.top,
                (long)( rcTab.right - rcTab.left ), (long)( rcTab.bottom - rcTab.top ) );
        printf( "  page rect:" );
        for( k = 0; k < nPages; k++ )
        {
            RECT rc;
            GetWindowRect( hPages[k], &rc );
            printf( " [%d]%ld,%ld %ldx%ld", k, (long)rc.left, (long)rc.top,
                    (long)( rc.right - rc.left ), (long)( rc.bottom - rc.top ) );
            /* The tab body starts below the label band, inside the tab rect. */
            if( rc.left >= rcTab.left && rc.top > rcTab.top + 10 &&
                rc.right <= rcTab.right && rc.bottom <= rcTab.bottom )
                nAtBodyTop++;
        }
        printf( "\n" );

        _snprintf( szDetail, sizeof szDetail,
                   "inside the tab body: %d of %d", nAtBodyTop, nPages );
        Check( "every page sits inside the tab body", nAtBodyTop == nPages, szDetail );
    }

    /* Give the dialog a moment to paint: the first page is shown inside
       WM_INITDIALOG, but its pixels are not on screen until the first paint, so
       sampling too early finds nothing and would look like a failure. */
    Sleep( 700 );

    {
        int nSel  = (int)SendMessageA( fm.hTab, TCM_GETCURSEL, 0, 0 );
        int nSeen = SeenPageIndex();
        char szDetail[128];
        _snprintf( szDetail, sizeof szDetail, "selected=%d seen=%d", nSel, nSeen );
        Check( "at start-up the tab body shows the selected page",
               nSel == 0 && nSeen == 0, szDetail );
    }

    for( nRound = 0; nRound < nRounds; nRound++ )
    {
        for( i = 0; i < 5; i++ )
        {
            int nSel, nSeen;
            char szWhat[128], szDetail[128];

            if( !ClickTabIndex( fm.hTab, i ) )
            {
                _snprintf( szWhat, sizeof szWhat,
                           "round %d: tab %d could be clicked", nRound, i );
                Check( szWhat, FALSE, "no click reached that label" );
                continue;
            }
            Sleep( 300 );

            nSel  = (int)SendMessageA( fm.hTab, TCM_GETCURSEL, 0, 0 );
            nSeen = SeenPageIndex();

            _snprintf( szWhat, sizeof szWhat,
                       "round %d: tab %d shows its own page in the tab body",
                       nRound, i );
            _snprintf( szDetail, sizeof szDetail,
                       "selected=%d seen=%d", nSel, nSeen );
            Check( szWhat, nSel == i && nSeen == i, szDetail );
        }
    }

    printf( "\n=== %d passed, %d failed ===\n", g_nPass, g_nFail );
    fflush( stdout );

    TerminateProcess( pi.hProcess, 0 );
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
    return ( g_nFail == 0 ) ? 0 : 1;
}
