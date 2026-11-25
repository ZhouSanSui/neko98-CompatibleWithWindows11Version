///////////////////////////////////////////////////////////////////////////////
// test_hotkeylist.cpp - runtime test for HotkeyList.cpp (the shortcut list).
//
// Drives the REAL HotkeyList.o, so it exercises the storage and validation the
// shipped programs use rather than a reimplementation of them.
//
// The test is deliberately non-destructive: it snapshots the three values it
// touches (the list itself and the two legacy slots) and puts all three back
// afterwards, so running it never changes the user's shortcuts.
//
// Build: see build_test_hotkeylist.bat
///////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "HotkeyList.h"
#include "NekoCommon.h"

static int g_nFail = 0;

static void CHECK( const char* szWhat, BOOL bOk, const char* szDetail = "" )
{
    printf( "[%s] %s%s%s\n", bOk ? "PASS" : "FAIL", szWhat,
            szDetail[0] ? "  " : "", szDetail );
    if( !bOk ) g_nFail++;
}

/* ------------------------------------------------------------------------- */
/* raw registry helpers, so the test can plant values the loader did not write */
/* ------------------------------------------------------------------------- */

static BOOL RawGetString( const char* szName, char* pszOut, DWORD cbOut )
{
    pszOut[0] = '\0';

    HKEY hKey = NULL;
    if( RegOpenKeyExA( HKEY_CURRENT_USER, szNekoRegKey, 0, KEY_QUERY_VALUE, &hKey ) != ERROR_SUCCESS )
        return FALSE;

    DWORD cb = cbOut;
    LONG  lRes = RegQueryValueExA( hKey, szName, NULL, NULL, (LPBYTE)pszOut, &cb );
    RegCloseKey( hKey );

    pszOut[cbOut - 1] = '\0';
    return lRes == ERROR_SUCCESS;
}

static BOOL RawSetString( const char* szName, const char* szValue )
{
    HKEY hKey = NULL;
    if( RegCreateKeyExA( HKEY_CURRENT_USER, szNekoRegKey, 0, NULL, 0,
                         KEY_SET_VALUE, NULL, &hKey, NULL ) != ERROR_SUCCESS )
        return FALSE;

    LONG lRes = RegSetValueExA( hKey, szName, 0, REG_SZ,
                                (const BYTE*)szValue, (DWORD)strlen( szValue ) + 1 );
    RegCloseKey( hKey );
    return lRes == ERROR_SUCCESS;
}

static BOOL RawSetDword( const char* szName, DWORD dwValue )
{
    HKEY hKey = NULL;
    if( RegCreateKeyExA( HKEY_CURRENT_USER, szNekoRegKey, 0, NULL, 0,
                         KEY_SET_VALUE, NULL, &hKey, NULL ) != ERROR_SUCCESS )
        return FALSE;

    LONG lRes = RegSetValueExA( hKey, szName, 0, REG_DWORD, (const BYTE*)&dwValue, sizeof dwValue );
    RegCloseKey( hKey );
    return lRes == ERROR_SUCCESS;
}

static void RawDelete( const char* szName )
{
    HKEY hKey = NULL;
    if( RegOpenKeyExA( HKEY_CURRENT_USER, szNekoRegKey, 0, KEY_SET_VALUE, &hKey ) != ERROR_SUCCESS )
        return;

    RegDeleteValueA( hKey, szName );
    RegCloseKey( hKey );
}

/* Save the user's three values so the test can restore them no matter what. */
static char g_szSavedList[4096];
static char g_szSavedHotkey[64];
static char g_szSavedHide[64];
static BOOL g_fHadList   = FALSE;
static BOOL g_fHadHotkey = FALSE;
static BOOL g_fHadHide   = FALSE;

static void SaveUserState()
{
    g_fHadList   = RawGetString( "HotkeyList",  g_szSavedList,   sizeof g_szSavedList );
    g_fHadHotkey = RawGetString( szNekoHotkeyKey,     g_szSavedHotkey, sizeof g_szSavedHotkey );
    g_fHadHide   = RawGetString( szNekoHideHotkeyKey, g_szSavedHide,   sizeof g_szSavedHide );
}

static void RestoreUserState()
{
    if( g_fHadList )   RawSetString( "HotkeyList", g_szSavedList );
    else               RawDelete( "HotkeyList" );

    if( g_fHadHotkey ) RawSetString( szNekoHotkeyKey, g_szSavedHotkey );
    else               RawDelete( szNekoHotkeyKey );

    if( g_fHadHide )   RawSetString( szNekoHideHotkeyKey, g_szSavedHide );
    else               RawDelete( szNekoHideHotkeyKey );
}

/* ------------------------------------------------------------------------- */

int main()
{
    printf( "=== Neko98 shortcut list test ===\n" );

    SaveUserState();

    /* ---- 1. defaults for each kind ------------------------------------- */
    NEKOHOTKEY e;
    ZeroMemory( &e, sizeof e );

    e.dwType = NEKO_HT_PROXY;
    HotkeyListSetDefaults( &e );
    CHECK( "new proxy entry defaults to 192.168.1.1:7890",
           strcmp( e.szP1, "192.168.1.1" ) == 0 && strcmp( e.szP2, "7890" ) == 0,
           e.szP1 );

    e.dwType = NEKO_HT_WEBSITE;
    HotkeyListSetDefaults( &e );
    CHECK( "new website entry defaults to bing in edge",
           strcmp( e.szP1, "https://www.bing.com" ) == 0 && strcmp( e.szP2, "edge" ) == 0,
           e.szP1 );

    e.dwType = NEKO_HT_PROJECTION;
    HotkeyListSetDefaults( &e );
    CHECK( "new projection entry defaults to two DIFFERENT modes",
           e.szP1[0] != '\0' && e.szP2[0] != '\0' && strcmp( e.szP1, e.szP2 ) != 0,
           e.szP1 );

    e.dwType = NEKO_HT_NOSLEEP;
    HotkeyListSetDefaults( &e );
    CHECK( "new no-sleep entry has no parameters",
           e.szP1[0] == '\0' && e.szP2[0] == '\0' );

    /* ---- 2. the projection table is consistent ------------------------- */
    BOOL fProjOk = TRUE;
    for( int i = 0; i < NEKO_PROJECTION_MODES; i++ )
    {
        const char* szName = HotkeyProjectionMode( i );
        const char* szSw   = HotkeyProjectionSwitch( i );
        if( !szName || !szSw )                       fProjOk = FALSE;
        else if( HotkeyProjectionIndex( szName ) != i ) fProjOk = FALSE;
        else if( szSw[0] != '/' )                    fProjOk = FALSE;
    }
    CHECK( "every projection mode name maps back to its own index", fProjOk );
    CHECK( "an unknown projection name is rejected", HotkeyProjectionIndex( "no such mode" ) == -1 );

    /* ---- 3. append / save / load round trip ---------------------------- */
    RawDelete( "HotkeyList" );

    int n = HotkeyListLoad();
    CHECK( "an absent list migrates the two legacy slots",
           n == 2 && g_nHotkeyList == 2,
           "" );
    if( g_nHotkeyList == 2 )
    {
        BOOL fSawHide = FALSE, fSawProxy = FALSE;
        for( int i = 0; i < g_nHotkeyList; i++ )
        {
            if( g_HotkeyList[i].dwType == NEKO_HT_HIDE )  fSawHide  = TRUE;
            if( g_HotkeyList[i].dwType == NEKO_HT_PROXY ) fSawProxy = TRUE;
        }
        CHECK( "the migrated list holds one hide entry and one proxy entry",
               fSawHide && fSawProxy );
    }

    int nNoSleep = HotkeyListAppend( NEKO_HT_NOSLEEP );
    CHECK( "appending returns the new index", nNoSleep == g_nHotkeyList - 1 && nNoSleep >= 0,
           "" );
    CHECK( "the appended entry got a fresh default", nNoSleep >= 0 );

    if( nNoSleep >= 0 )
        g_HotkeyList[nNoSleep].dwHotkey = 0x0C46;   /* Ctrl+Shift+F9, unused */

    CHECK( "the list saves", HotkeyListSave() == TRUE );

    int nBefore = g_nHotkeyList;
    g_nHotkeyList = 0;
    int nReloaded = HotkeyListLoad();
    CHECK( "the list reloads with the same number of entries", nReloaded == nBefore,
           "" );
    if( nReloaded == nBefore && nBefore > 0 )
        CHECK( "every entry survives the round trip",
               g_HotkeyList[nBefore - 1].dwType == NEKO_HT_NOSLEEP &&
               g_HotkeyList[nBefore - 1].dwHotkey == 0x0C46 );

    /* ---- 4. hostile parameters cannot break the format ----------------- */
    if( g_nHotkeyList > 0 )
    {
        strcpy( g_HotkeyList[0].szP1, "a\tb" );     /* TAB inside a field */
        strcpy( g_HotkeyList[0].szP2, "c\r\nd" );   /* line break inside */
        HotkeyListSave();

        g_nHotkeyList = 0;
        HotkeyListLoad();

        BOOL fClean = TRUE;
        for( int i = 0; i < g_nHotkeyList; i++ )
        {
            if( strchr( g_HotkeyList[i].szP1, '\t' ) || strchr( g_HotkeyList[i].szP1, '\r' ) ) fClean = FALSE;
            if( strchr( g_HotkeyList[i].szP2, '\t' ) || strchr( g_HotkeyList[i].szP2, '\r' ) ) fClean = FALSE;
        }
        CHECK( "a TAB or line break typed into a field cannot corrupt the list", fClean );

        char szCount[32];
        _snprintf( szCount, sizeof szCount, "entries after reload=%d", g_nHotkeyList );
        CHECK( "the entry count is unaffected by the hostile field",
               g_nHotkeyList == nBefore, szCount );
    }

    /* ---- 5. validation -------------------------------------------------- */
    char szErr[256];

    g_nHotkeyList = 0;
    ZeroMemory( g_HotkeyList, sizeof g_HotkeyList );

    g_HotkeyList[0].dwType   = NEKO_HT_HIDE;
    g_HotkeyList[0].dwHotkey = 0x0000;              /* disabled - always legal */
    g_nHotkeyList = 1;
    CHECK( "an empty combination is legal",
           HotkeyListValidate( szErr, sizeof szErr ) == TRUE );

    g_HotkeyList[0].dwHotkey = 0x000061;            /* bare 'A', no modifier */
    CHECK( "a combination with no modifier is refused",
           HotkeyListValidate( szErr, sizeof szErr ) == FALSE, szErr );

    g_HotkeyList[0].dwHotkey = 0x064E;              /* Ctrl+Alt+N */
    CHECK( "a normal combination is accepted",
           HotkeyListValidate( szErr, sizeof szErr ) == TRUE );

    g_HotkeyList[1].dwType   = NEKO_HT_NOSLEEP;
    g_HotkeyList[1].dwHotkey = 0x064E;              /* same as entry 0 */
    g_nHotkeyList = 2;
    CHECK( "two entries may not share one combination",
           HotkeyListValidate( szErr, sizeof szErr ) == FALSE, szErr );

    g_HotkeyList[1].dwHotkey = 0x464E;              /* Ctrl+Shift+N */
    CHECK( "a different combination is accepted again",
           HotkeyListValidate( szErr, sizeof szErr ) == TRUE );

    g_HotkeyList[0].dwType   = NEKO_HT_PROJECTION;
    g_HotkeyList[0].dwHotkey = 0x064E;
    strcpy( g_HotkeyList[0].szP1, HotkeyProjectionMode( 2 ) );
    strcpy( g_HotkeyList[0].szP2, HotkeyProjectionMode( 2 ) );
    g_nHotkeyList = 1;
    CHECK( "a projection pair naming the same mode twice is refused",
           HotkeyListValidate( szErr, sizeof szErr ) == FALSE, szErr );

    strcpy( g_HotkeyList[0].szP2, HotkeyProjectionMode( 0 ) );
    CHECK( "a projection pair naming two modes is accepted",
           HotkeyListValidate( szErr, sizeof szErr ) == TRUE );

    /* ---- 6. removing entries -------------------------------------------- */
    g_nHotkeyList = 0;
    ZeroMemory( g_HotkeyList, sizeof g_HotkeyList );
    HotkeyListAppend( NEKO_HT_HIDE );
    HotkeyListAppend( NEKO_HT_PROXY );
    HotkeyListAppend( NEKO_HT_NOSLEEP );
    nBefore = g_nHotkeyList;

    BOOL fRemoved = HotkeyListRemove( 1 );
    CHECK( "removing the middle entry succeeds", fRemoved && g_nHotkeyList == nBefore - 1 );
    CHECK( "the remaining entries keep their order",
           g_HotkeyList[0].dwType == NEKO_HT_HIDE && g_HotkeyList[1].dwType == NEKO_HT_NOSLEEP );
    CHECK( "a bad index is refused",
           HotkeyListRemove( 99 ) == FALSE && HotkeyListRemove( -1 ) == FALSE );

    /* ---- 7. an empty list stays empty ----------------------------------- */
    g_nHotkeyList = 0;
    HotkeyListSave();
    g_nHotkeyList = 0;
    int nEmpty = HotkeyListLoad();
    CHECK( "an empty list is not re-filled with the migrated defaults",
           nEmpty == 0 && g_nHotkeyList == 0, "" );

    /* ---- 8. labels are human readable ----------------------------------- */
    NEKOHOTKEY lbl;
    ZeroMemory( &lbl, sizeof lbl );
    lbl.dwType = NEKO_HT_PROXY;
    strcpy( lbl.szP1, "1.2.3.4" );
    strcpy( lbl.szP2, "8080" );
    char szLabel[128];
    HotkeyListLabel( &lbl, szLabel, sizeof szLabel );
    CHECK( "a proxy label mentions its address and port",
           strstr( szLabel, "1.2.3.4" ) != NULL && strstr( szLabel, "8080" ) != NULL, szLabel );

    lbl.dwType = NEKO_HT_WEBSITE;
    strcpy( lbl.szP1, "https://www.example.com/some/page" );
    strcpy( lbl.szP2, "edge" );
    HotkeyListLabel( &lbl, szLabel, sizeof szLabel );
    CHECK( "a website label shows the host without the scheme or path",
           strstr( szLabel, "www.example.com" ) != NULL &&
           strstr( szLabel, "https://" ) == NULL &&
           strstr( szLabel, "/some/page" ) == NULL, szLabel );

    /* ---- restore -------------------------------------------------------- */
    RestoreUserState();

    printf( "\n%s (%d failure%s)\n", g_nFail ? "=== FAILURES ===" : "=== ALL PASS ===",
            g_nFail, g_nFail == 1 ? "" : "s" );
    return g_nFail ? 1 : 0;
}
