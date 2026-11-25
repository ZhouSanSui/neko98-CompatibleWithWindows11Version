///////////////////////////////////////////////////////////////////////////////
// Actions.cpp - see Actions.h for the rationale behind each of the three.
///////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include "Actions.h"
#include "HotkeyList.h"


/*=============================================================================
  Keep the machine awake
=============================================================================*/

static BOOL g_bSleepGuardActive = FALSE;

BOOL SleepGuardIsActive()
{
    return g_bSleepGuardActive;
}

BOOL SleepGuardSetActive( BOOL bActive )
{
    EXECUTION_STATE es;

    if( bActive )
    {
        //ES_CONTINUOUS makes the request stand until it is cleared, instead of
        //timing out like a one-shot SetThreadExecutionState call would.
        es = SetThreadExecutionState( ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED );
        if( es == 0 ) return FALSE;          //the call failed; do not claim success
        g_bSleepGuardActive = TRUE;
        return TRUE;
    }

    //Clear both bits: leaving ES_SYSTEM_REQUIRED set would keep the machine up
    //even though the user asked for normal behaviour again.
    SetThreadExecutionState( ES_CONTINUOUS );
    g_bSleepGuardActive = FALSE;
    return TRUE;
}

BOOL SleepGuardToggle()
{
    //Return the state the machine is in AFTER the toggle, not the success flag:
    //the caller (and the test) wants "is sleep being blocked now?", and a failed
    //request leaves the flag clear, which reads correctly as "not blocked".
    SleepGuardSetActive( g_bSleepGuardActive ? FALSE : TRUE );
    return g_bSleepGuardActive;
}


/*=============================================================================
  Open a page in a browser
=============================================================================*/

static const char* g_szBrowserId[3]   = { "edge", "chrome", "custom" };
static const char* g_szBrowserName[3] = { "Edge", "Chrome", "自定义" };

const char* WebLauncherBrowserId( int nBrowser )
{
    if( nBrowser < 0 || nBrowser > 2 ) return g_szBrowserId[0];
    return g_szBrowserId[nBrowser];
}

const char* WebLauncherBrowserName( int nBrowser )
{
    if( nBrowser < 0 || nBrowser > 2 ) return g_szBrowserName[0];
    return g_szBrowserName[nBrowser];
}

int WebLauncherBrowserFromId( const char* szId )
{
    int i;
    if( szId == NULL ) return NEKO_BROWSER_EDGE;
    for( i = 0; i < 3; i++ )
        if( _stricmp( szId, g_szBrowserId[i] ) == 0 ) return i;
    return NEKO_BROWSER_EDGE;
}


BOOL WebLauncherFindExecutable( int nBrowser, char* szOut, int cbOut )
{
    char szTmp[MAX_PATH];
    DWORD dwAttr;

    if( szOut == NULL || cbOut <= 0 ) return FALSE;
    szOut[0] = '\0';

    switch( nBrowser )
    {
        case NEKO_BROWSER_EDGE:
        case NEKO_BROWSER_CHROME:
            break;

        default:
            return FALSE;
    }

    //Shared lookup: try App Paths for the well-known names first, then the two
    //standard install locations.
    {
        const char* szExe = ( nBrowser == NEKO_BROWSER_EDGE ) ? "msedge.exe" : "chrome.exe";
        char        szKey[MAX_PATH];
        DWORD       cb;

        _snprintf( szKey, sizeof szKey,
                   "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%s", szExe );
        szKey[sizeof szKey - 1] = '\0';

        cb = (DWORD)cbOut;
        if( RegGetValueA( HKEY_LOCAL_MACHINE, szKey, NULL, RRF_RT_REG_SZ,
                          NULL, szOut, &cb ) == ERROR_SUCCESS )
        {
            char        szUnquoted[MAX_PATH];
            const char* p = szOut;
            int         n = 0;

            if( *p == '"' ) p++;
            while( *p != '\0' && *p != '"' && n < (int)sizeof szUnquoted - 1 )
                szUnquoted[n++] = *p++;
            szUnquoted[n] = '\0';

            if( n > 0 )
            {
                dwAttr = GetFileAttributesA( szUnquoted );
                if( dwAttr != INVALID_FILE_ATTRIBUTES && !( dwAttr & FILE_ATTRIBUTE_DIRECTORY ) )
                {
                    strncpy( szOut, szUnquoted, cbOut - 1 );
                    szOut[cbOut - 1] = '\0';
                    return TRUE;
                }
            }
        }

        //Fall back to the documented install roots.
        {
            const char* aRoot[2];
            int         i;

            if( nBrowser == NEKO_BROWSER_CHROME )
            {
                aRoot[0] = "C:\\Program Files\\Google\\Chrome\\Application\\";
                aRoot[1] = "C:\\Program Files (x86)\\Google\\Chrome\\Application\\";
            }
            else
            {
                aRoot[0] = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\";
                aRoot[1] = "C:\\Program Files\\Microsoft\\Edge\\Application\\";
            }

            for( i = 0; i < 2; i++ )
            {
                _snprintf( szTmp, sizeof szTmp, "%s%s", aRoot[i], szExe );
                szTmp[sizeof szTmp - 1] = '\0';
                dwAttr = GetFileAttributesA( szTmp );
                if( dwAttr != INVALID_FILE_ATTRIBUTES && !( dwAttr & FILE_ATTRIBUTE_DIRECTORY ) )
                {
                    strncpy( szOut, szTmp, cbOut - 1 );
                    szOut[cbOut - 1] = '\0';
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}


BOOL WebLauncherOpen( const char* szUrl, const char* szBrowserId, const char* szCustomPath )
{
    int  nBrowser;
    char szTarget[MAX_PATH];
    char szParams[2048];

    if( szUrl == NULL || szUrl[0] == '\0' ) return FALSE;

    nBrowser = WebLauncherBrowserFromId( szBrowserId );

    if( nBrowser == NEKO_BROWSER_CUSTOM )
    {
        if( szCustomPath == NULL || szCustomPath[0] == '\0' ) return FALSE;
        strncpy( szTarget, szCustomPath, sizeof szTarget - 1 );
        szTarget[sizeof szTarget - 1] = '\0';
    }
    else
    {
        if( !WebLauncherFindExecutable( nBrowser, szTarget, sizeof szTarget ) )
        {
            //Last resort: let the shell resolve the well-known image name. This
            //still works on machines where the App Paths key is missing.
            strcpy( szTarget, ( nBrowser == NEKO_BROWSER_CHROME ) ? "chrome.exe" : "msedge.exe" );
        }
    }

    //Quote the URL so spaces survive, and pass it as a SEPARATE argument rather
    //than gluing it into a command line: a URL containing '&' must not be able
    //to start a second command.
    _snprintf( szParams, sizeof szParams, "\"%s\"", szUrl );
    szParams[sizeof szParams - 1] = '\0';

    {
        HINSTANCE hInst = ShellExecuteA( NULL, "open", szTarget, szParams, NULL, SW_SHOWNORMAL );
        //ShellExecute reports success as a value greater than 32; anything else
        //is an error code. Without this check a mistyped browser path would look
        //like a working shortcut that simply does nothing.
        if( (INT_PTR)hInst <= 32 ) return FALSE;
    }

    return TRUE;
}


/*=============================================================================
  Projection
=============================================================================*/

BOOL ProjectionApply( int nModeIndex )
{
    char szSwitch[MAX_PATH];
    char szExe[MAX_PATH];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    BOOL bOk;

    if( nModeIndex < 0 || nModeIndex >= NEKO_PROJECTION_MODES ) return FALSE;

    //DisplaySwitch.exe takes the mode as its only argument.
    GetSystemDirectoryA( szExe, MAX_PATH );
    strncat( szExe, "\\DisplaySwitch.exe ", MAX_PATH - strlen( szExe ) - 1 );
    strncat( szExe, HotkeyProjectionSwitch( nModeIndex ), MAX_PATH - strlen( szExe ) - 1 );

    strncpy( szSwitch, szExe, sizeof szSwitch - 1 );
    szSwitch[sizeof szSwitch - 1] = '\0';

    memset( &si, 0, sizeof si );
    si.cb = sizeof si;
    memset( &pi, 0, sizeof pi );

    //CreateProcess wants a writable command line, hence the copy above.
    bOk = CreateProcessA( NULL, szSwitch, NULL, NULL, FALSE,
                          0, NULL, NULL, &si, &pi );
    if( !bOk )
    {
        //Some builds only ship the executable under Sysnative; fall back to
        //ShellExecute, which goes through the shell's own resolution.
        HINSTANCE hInst = ShellExecuteA( NULL, "open", "DisplaySwitch.exe",
                                        HotkeyProjectionSwitch( nModeIndex ), NULL, SW_SHOWNORMAL );
        return ( (INT_PTR)hInst > 32 ) ? TRUE : FALSE;
    }

    //We only need to know it started; the switch happens in that process.
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );
    return TRUE;
}


//Remember which of the two modes each shortcut applied last, so consecutive
//presses alternate. Keyed by the pair of mode names, because two entries could
//in principle describe the same pair and should then alternate together.
typedef struct tagPROJSTATE
{
    char szKey[64];
    int  nLast;
} PROJSTATE;

static PROJSTATE g_aProjState[8];
static int       g_nProjState = 0;

int ProjectionToggle( const char* szModeA, const char* szModeB )
{
    char szKey[64];
    int  iA = HotkeyProjectionIndex( szModeA );
    int  iB = HotkeyProjectionIndex( szModeB );
    int  nNext;

    if( iA < 0 || iB < 0 || iA == iB ) return -1;

    _snprintf( szKey, sizeof szKey, "%d:%d", iA, iB );
    szKey[sizeof szKey - 1] = '\0';

    //Find (or record) the state slot for this pair.
    {
        int nSlot = -1;
        for( int i = 0; i < g_nProjState; i++ )
        {
            if( strcmp( g_aProjState[i].szKey, szKey ) == 0 ) { nSlot = i; break; }
        }
        if( nSlot < 0 )
        {
            if( g_nProjState < (int)( sizeof g_aProjState / sizeof g_aProjState[0] ) )
            {
                nSlot = g_nProjState++;
            }
            else
            {
                nSlot = 0;                      //recycle rather than grow forever
            }
            strncpy( g_aProjState[nSlot].szKey, szKey, sizeof g_aProjState[nSlot].szKey - 1 );
            g_aProjState[nSlot].szKey[sizeof g_aProjState[nSlot].szKey - 1] = '\0';
            g_aProjState[nSlot].nLast = iB;     //so the FIRST press applies A
        }

        //Alternate: if we last applied A, apply B, and vice versa.
        nNext = ( g_aProjState[nSlot].nLast == iA ) ? iB : iA;

        if( !ProjectionApply( nNext ) ) return -1;

        g_aProjState[nSlot].nLast = nNext;
    }

    return nNext;
}
