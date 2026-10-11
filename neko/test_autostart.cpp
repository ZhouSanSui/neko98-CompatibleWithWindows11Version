// test_autostart.cpp - runtime test for the "start with Windows" feature.
//
// It links the REAL AutoStart.o and drives the real functions against the real
// HKCU Run key, so it proves the shipped behaviour rather than a lookalike:
//
//   * NekoAutoStartSet(TRUE)  writes the Run value and IsEnabled() sees it
//   * the Run value names the PET (Neko98.exe), never the program that happened
//     to write it - this code is shared with the settings program, and pointing
//     the Run key at NekoCFG.exe would open the settings dialog at every logon
//   * with no pet beside it, Set(TRUE) FAILS instead of registering the wrong
//     program
//   * NekoAutoStartSet(FALSE) removes it and IsEnabled() reports disabled
//   * IsEnabled() does NOT trust a value that points somewhere else (the
//     "program was moved" case), which is what makes the checkbox honest
//   * Repair() leaves an untouched system alone and fixes a stale path
//
// SAFETY: the Run value is saved before the test and restored afterwards, and a
// placeholder pet file the test creates for itself is removed again, so a
// failure part-way through cannot leave the user's system altered.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "AutoStart.h"

static const char* g_szRunKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static int g_nFail = 0;
static int g_nSkip = 0;

static void CHECK( const char* what, BOOL ok, const char* detail )
{
    printf( "[%s] %-56s %s\n", ok ? "PASS" : "FAIL", what, detail ? detail : "" );
    if( !ok ) g_nFail++;
}

static void SKIP( const char* what, const char* why )
{
    printf( "[SKIP] %-56s %s\n", what, why ? why : "" );
    g_nSkip++;
}

//raw access, deliberately bypassing AutoStart.cpp so the test can plant values
static BOOL RawGet( char* pszOut, DWORD cbOut, DWORD* pdwType )
{
    HKEY hKey = NULL;
    if( RegOpenKeyEx( HKEY_CURRENT_USER, g_szRunKey, 0, KEY_READ, &hKey ) != ERROR_SUCCESS )
        return FALSE;

    DWORD dwSize = cbOut;
    LONG lRes = RegQueryValueEx( hKey, NEKO_RUN_VALUE, NULL, pdwType, (LPBYTE)pszOut, &dwSize );
    RegCloseKey( hKey );
    return ( lRes == ERROR_SUCCESS );
}

static BOOL RawSet( const char* pszValue )
{
    HKEY hKey = NULL;
    if( RegCreateKeyEx( HKEY_CURRENT_USER, g_szRunKey, 0, NULL, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, NULL, &hKey, NULL ) != ERROR_SUCCESS )
        return FALSE;
    LONG lRes = RegSetValueEx( hKey, NEKO_RUN_VALUE, 0, REG_SZ,
                               (const BYTE*)pszValue, (DWORD)strlen(pszValue) + 1 );
    RegCloseKey( hKey );
    return ( lRes == ERROR_SUCCESS );
}

static void RawDelete()
{
    HKEY hKey = NULL;
    if( RegOpenKeyEx( HKEY_CURRENT_USER, g_szRunKey, 0, KEY_SET_VALUE, &hKey ) != ERROR_SUCCESS )
        return;
    RegDeleteValue( hKey, NEKO_RUN_VALUE );
    RegCloseKey( hKey );
}

/* TRUE when pszPath ends with pszName, case-insensitively. A stored Run value is
   wrapped in quotes, so they are ignored on both ends.

   NOTE: the comparison must be length-bounded. lstrcmpi(p + (a-b), name) would
   keep comparing past the name and trip over the closing quote, so it always
   reported a mismatch even on an exact path. */
static BOOL EndsWith( const char* pszPath, const char* pszName )
{
    if( pszPath == NULL || pszName == NULL ) return FALSE;

    const char* p = pszPath;
    size_t a = strlen( p );

    //drop the surrounding quotes the Run value is written with
    if( a >= 2 && p[0] == '"' && p[a-1] == '"' ) { p++; a -= 2; }

    size_t b = strlen( pszName );
    if( a < b ) return FALSE;
    return ( _strnicmp( p + ( a - b ), pszName, b ) == 0 );
}

/* ------------------------------------------------------------------

   Focused child mode: when this program is launched from inside a
   throw-away extraction folder (7-Zip unpacks our installer into
   "%TEMP%\7zXXXX.tmp" and deletes it on exit), Repair() must refuse to
   adopt that path, or auto-start would point at a folder that no longer
   exists by the next logon.

   Run as:  test_autostart.exe --repair-in-throwaway <expectedValueBase64ish>
   We keep it simple: the child stores a sentinel path itself, calls
   Repair(), and exits 0 only when the sentinel survived untouched.

   ------------------------------------------------------------------ */
static int RunThrowAwayCheck()
{
    printf( "=== throw-away-folder repair check (child) ===\n" );

    //save whatever the user had, so this check stays non-destructive too
    char szSaved[1024];
    DWORD dwSavedType = 0;
    BOOL  bHadSaved = RawGet( szSaved, sizeof(szSaved), &dwSavedType );

    char szSelf[MAX_PATH];
    GetModuleFileName( NULL, szSelf, MAX_PATH );
    printf( "running from: %s\n", szSelf );

    //sanity: the parent is supposed to have placed us in a 7z*.tmp folder, so
    //if this check ever runs anywhere else it proves nothing and must say so.
    //The name that matters is the FOLDER's, not our own file name.
    char szDir[MAX_PATH];
    lstrcpyn( szDir, szSelf, MAX_PATH );
    char* pSlash = strrchr( szDir, '\\' );
    if( pSlash != NULL ) *pSlash = '\0';

    const char* pszLeaf = strrchr( szDir, '\\' );
    pszLeaf = pszLeaf ? ( pszLeaf + 1 ) : szDir;

    BOOL bNamesThrowAway = ( _strnicmp( pszLeaf, "7z", 2 ) == 0 );

    const char* pszSentinel = "\"C:\\NekoThrowAwaySentinel\\Neko98.exe\"";
    RawSet( pszSentinel );

    BOOL bRepaired = NekoAutoStartRepair();

    char szNow[1024]; DWORD t = 0;
    BOOL bGot = RawGet( szNow, sizeof(szNow), &t );

    printf( "leaf='%s' throwaway=%d Repair()=%d now=%s\n",
            pszLeaf, bNamesThrowAway ? 1 : 0, bRepaired ? 1 : 0,
            bGot ? szNow : "(none)" );

    CHECK( "child really runs from a 7z*.tmp folder", bNamesThrowAway != FALSE, pszLeaf );
    CHECK( "Repair() refuses to adopt a throw-away extraction path",
           bRepaired == FALSE, NULL );
    CHECK( "the stored value was left untouched",
           bGot && lstrcmpi( szNow, pszSentinel ) == 0, szNow );

    //put the user's entry back before we exit
    if( bHadSaved ) RawSet( szSaved );
    else            RawDelete();

    printf( "\n=== %s (%d failures, %d skipped) ===\n",
            g_nFail ? "FAILED" : "ALL PASS", g_nFail, g_nSkip );
    return g_nFail ? 2 : 0;
}

int main( int argc, char** argv )
{
    if( argc > 1 && strcmp( argv[1], "--repair-in-throwaway" ) == 0 )
        return RunThrowAwayCheck();

    printf( "=== Neko98 auto-start test ===\n" );

    //---- save whatever the user already had, so this test is non-destructive
    char szSaved[1024];
    DWORD dwSavedType = 0;
    BOOL  bHadSaved = RawGet( szSaved, sizeof(szSaved), &dwSavedType );

    printf( "existing Run value: %s\n", bHadSaved ? szSaved : "(absent)" );

    char buf[1024];
    buf[0] = '\0';

    //---- work out the value the program is REQUIRED to store ----
    //It must name the pet next to us, not the running module itself.
    char szSelf[MAX_PATH];
    GetModuleFileName( NULL, szSelf, MAX_PATH );

    char szDir[MAX_PATH];
    lstrcpyn( szDir, szSelf, MAX_PATH );
    char* pszSlash = strrchr( szDir, '\\' );
    if( pszSlash != NULL ) *( pszSlash + 1 ) = '\0';

    char szPet[MAX_PATH];
    lstrcpyn( szPet, szDir, MAX_PATH );
    lstrcat( szPet, NEKO_PET_EXE );

    char szWant[MAX_PATH + 8];
    wsprintf( szWant, "\"%s\"", szPet );

    //Make sure the pet really exists beside us. A zero-byte placeholder is
    //enough: nothing is executed, GetFileAttributes is the only thing consulted.
    BOOL bHadPet = ( GetFileAttributes( szPet ) != INVALID_FILE_ATTRIBUTES );
    BOOL bMadePet = FALSE;
    if( !bHadPet )
    {
        HANDLE hFile = CreateFile( szPet, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, NULL );
        if( hFile != INVALID_HANDLE_VALUE )
        {
            CloseHandle( hFile );
            bMadePet = TRUE;
            bHadPet = TRUE;
        }
    }

    printf( "running module : %s\n", szSelf );
    printf( "pet the Run key must name : %s (%s)\n",
            szPet, bHadPet ? "present" : "MISSING" );

    //A pet must never be recorded as starting up, and neither should the
    //settings program, so the stored value must not be our own path.
    CHECK( "the pet being registered is not this program",
           lstrcmpi( szSelf, szPet ) != 0, szSelf );

    //---- clean slate ----
    RawDelete();
    CHECK( "starts disabled after a clean delete", NekoAutoStartIsEnabled() == FALSE, buf );

    //---- enable ----
    BOOL bSet = NekoAutoStartSet( TRUE );
    if( !bHadPet )
    {
        //Without a pet to point at, registering our own path would be worse
        //than failing: Windows would open this program at every logon.
        CHECK( "with no pet present, enabling FAILS instead of storing the wrong program",
               bSet == FALSE, buf );
        CHECK( "nothing was written when the pet was missing",
               NekoAutoStartIsEnabled() == FALSE, buf );
        RawDelete();
        SKIP( "pet-path assertions", "no pet file could be created beside the test" );
    }
    else
    {
        CHECK( "NekoAutoStartSet(TRUE) succeeds", bSet != FALSE, buf );
        CHECK( "IsEnabled() reports enabled right after", NekoAutoStartIsEnabled() != FALSE, buf );

        char szNow[1024];
        DWORD dwType = 0;
        BOOL bPresent = RawGet( szNow, sizeof(szNow), &dwType );
        CHECK( "Run value really exists in the registry", bPresent != FALSE, buf );
        CHECK( "value is REG_SZ", bPresent && dwType == REG_SZ, buf );

        sprintf( buf, "stored=%s  want=%s", bPresent ? szNow : "(none)", szWant );
        CHECK( "stored value names the PET beside us",
               bPresent && lstrcmpi( szNow, szWant ) == 0, buf );

        //This is the defect the test exists to prevent. The trailing text is
        //printed so a mismatch can be diagnosed without re-running anything.
        {
            int nLen = (int)strlen( szNow );
            char szTail[24];
            lstrcpyn( szTail, szNow + ( ( nLen > 18 ) ? nLen - 18 : 0 ), 19 );
            sprintf( buf, "tail='%s' petName='%s' len=%d", szTail, NEKO_PET_EXE, nLen );
        }
        CHECK( "stored value is NOT the settings program",
               bPresent && !EndsWith( szNow, "NekoCFG.exe" ) && EndsWith( szNow, NEKO_PET_EXE ), buf );

        //a quoted path is what makes a directory with spaces work
        CHECK( "stored value is quoted (survives spaces in the path)",
               bPresent && szNow[0] == '"' && szNow[strlen(szNow)-1] == '"', buf );

        //---- repair must be a no-op when the value is already correct ----
        BOOL bRepaired = NekoAutoStartRepair();
        sprintf( buf, "Repair() returned %d on a correct value", bRepaired ? 1 : 0 );
        CHECK( "Repair() does not rewrite a correct entry", bRepaired == FALSE, buf );

        //---- a stale path must NOT read as enabled ----
        RawSet( "\"C:\\Somewhere\\Else\\Neko98.exe\"" );
        CHECK( "stale path does NOT read as enabled",
               NekoAutoStartIsEnabled() == FALSE, buf );

        //...but Repair() must fix it, because the user DID ask for auto-start
        BOOL bFixed = NekoAutoStartRepair();
        CHECK( "Repair() fixes a stale path", bFixed != FALSE, buf );

        char szFixed[1024]; DWORD t2 = 0;
        BOOL bGot = RawGet( szFixed, sizeof(szFixed), &t2 );
        sprintf( buf, "fixed=%s", bGot ? szFixed : "(none)" );
        CHECK( "repaired path points at the pet",
               bGot && lstrcmpi( szFixed, szWant ) == 0, buf );
        CHECK( "IsEnabled() true again after repair",
               NekoAutoStartIsEnabled() != FALSE, buf );

        /* ---- Repair() must NOT steal a working entry ----

           Repair exists to rescue a path that went stale. When the stored path
           still resolves to a real pet file, the user has a working install, so
           running a SECOND copy from somewhere else (a scratch folder, a test
           build, or the throw-away directory the self-extracting installer
           unpacks into) must leave that entry alone. Without this, launching
           any copy would silently repoint auto-start at itself - and in the
           installer case at a folder 7-Zip deletes on exit. */
        {
            char szAltDir[MAX_PATH];
            lstrcpyn( szAltDir, szDir, MAX_PATH );
            char* pSlash = strrchr( szAltDir, '\\' );
            if( pSlash != NULL ) *( pSlash + 1 ) = '\0';
            lstrcat( szAltDir, "neko_alt_install" );

            CreateDirectory( szAltDir, NULL );

            char szAltPet[MAX_PATH];
            lstrcpyn( szAltPet, szAltDir, MAX_PATH );
            lstrcat( szAltPet, "\\" );
            lstrcat( szAltPet, NEKO_PET_EXE );

            HANDLE hAlt = CreateFile( szAltPet, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, NULL );
            if( hAlt != INVALID_HANDLE_VALUE )
            {
                CloseHandle( hAlt );

                char szAltValue[MAX_PATH + 8];
                wsprintf( szAltValue, "\"%s\"", szAltPet );
                RawSet( szAltValue );

                //It is a real pet file, but not the one beside us, so the
                //settings checkbox must show "not this copy"...
                CHECK( "an entry naming a DIFFERENT but real pet does not read as enabled",
                       NekoAutoStartIsEnabled() == FALSE, szAltValue );

                //...and Repair() must leave it pointing there, untouched.
                BOOL bStole = NekoAutoStartRepair();

                char szKept[1024]; DWORD t6 = 0;
                BOOL bK = RawGet( szKept, sizeof(szKept), &t6 );
                sprintf( buf, "Repair()=%d kept=%s", bStole ? 1 : 0,
                         bK ? szKept : "(none)" );
                CHECK( "Repair() does not hijack an entry that still points at a real pet",
                       bStole == FALSE && bK && lstrcmpi( szKept, szAltValue ) == 0, buf );

                DeleteFile( szAltPet );
            }
            else
            {
                SKIP( "Repair() does not hijack a working entry",
                      "could not create an alternative pet file" );
            }

            RemoveDirectory( szAltDir );
        }

        //---- a value planted at the settings program must not read as enabled,
        //---- and Repair() must migrate it to the pet ----
        {
            char szCfg[MAX_PATH + 8];
            lstrcpyn( szCfg, szDir, MAX_PATH );
            lstrcat( szCfg, "NekoCFG.exe" );
            char szPlanted[MAX_PATH + 12];
            wsprintf( szPlanted, "\"%s\"", szCfg );

            RawSet( szPlanted );
            CHECK( "a value pointing at the settings program does not read as enabled",
                   NekoAutoStartIsEnabled() == FALSE, szPlanted );

            BOOL bMig = NekoAutoStartRepair();
            char szAfter[1024]; DWORD t5 = 0;
            BOOL bG = RawGet( szAfter, sizeof(szAfter), &t5 );
            sprintf( buf, "Repair()=%d now=%s", bMig ? 1 : 0, bG ? szAfter : "(none)" );
            CHECK( "Repair() migrates a settings-program entry to the pet",
                   bMig != FALSE && bG && lstrcmpi( szAfter, szWant ) == 0, buf );
        }
    }

    //---- repair must leave an absent entry absent (user never opted in) ----
    {
        RawDelete();
        char szNow[1024];
        BOOL bR = NekoAutoStartRepair();
        DWORD t3 = 0;
        BOOL bStill = RawGet( szNow, sizeof(szNow), &t3 );
        sprintf( buf, "Repair()=%d, value present=%d", bR ? 1 : 0, bStill ? 1 : 0 );
        CHECK( "Repair() does not create an entry the user never enabled",
               bR == FALSE && bStill == FALSE, buf );
    }

    //---- disable ----
    NekoAutoStartSet( TRUE );
    BOOL bOff = NekoAutoStartSet( FALSE );
    CHECK( "NekoAutoStartSet(FALSE) succeeds", bOff != FALSE, NULL );
    CHECK( "IsEnabled() reports disabled after removal",
           NekoAutoStartIsEnabled() == FALSE, NULL );

    //disabling twice must still succeed (already absent is fine)
    CHECK( "disabling twice still succeeds",
           NekoAutoStartSet( FALSE ) != FALSE, NULL );

    //---- restore exactly what the user had ----
    if( bHadSaved ) RawSet( szSaved );
    else            RawDelete();

    char szFinal[1024];
    DWORD t4 = 0;
    BOOL bRestored = RawGet( szFinal, sizeof(szFinal), &t4 );
    sprintf( buf, "restored present=%d value=%s", bRestored ? 1 : 0,
             bRestored ? szFinal : "(absent)" );
    CHECK( "user's original Run state was restored",
           bHadSaved ? ( bRestored && lstrcmpi( szFinal, szSaved ) == 0 ) : ( bRestored == FALSE ),
           buf );

    //---- remove only the placeholder we created ourselves ----
    if( bMadePet ) DeleteFile( szPet );

    printf( "\n=== %s (%d failures, %d skipped) ===\n",
            g_nFail ? "FAILED" : "ALL PASS", g_nFail, g_nSkip );
    return g_nFail ? 2 : 0;
}
