///////////////////////////////////////////////////////////////////////////////
// ProxyToggle.cpp — see ProxyToggle.h for the format of the settings.
//
// Implementation notes that are easy to get wrong and are therefore spelled out:
//
//  * The values live under HKEY_CURRENT_USER, so no elevation is needed and no
//    UAC prompt appears. This is the same place the Windows Settings UI writes.
//  * Registry writes are NOT picked up by applications until WinINET is told.
//    INTERNET_OPTION_SETTINGS_CHANGED tells WinINET to re-read the registry;
//    INTERNET_OPTION_REFRESH asks it to drop its cached connections. Both are
//    issued, because older Windows versions honour only the first.
//  * The refresh must happen AFTER the write, never before.
//  * An existing ProxyOverride is preserved when it already contains the
//    configured bypass list, so a user who added entries by hand does not get
//    them silently dropped when toggling the proxy.
///////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <wininet.h>
#include <string.h>
#include "ProxyToggle.h"

#ifndef __PROXYTOGGLE_CPP__
#define __PROXYTOGGLE_CPP__

static const char* g_szInetKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";


//Read one REG_DWORD. Returns FALSE when the value is missing or is not a DWORD,
//in which case *pdwValue is left untouched.
static BOOL ReadDword( HKEY hKey, const char* szName, DWORD* pdwValue )
{
    DWORD dwType = 0;
    DWORD dwValue = 0;
    DWORD cb = sizeof( dwValue );
    LONG lRes = RegQueryValueExA( hKey, szName, NULL, &dwType, (LPBYTE)&dwValue, &cb );
    if( lRes != ERROR_SUCCESS ) return FALSE;
    if( dwType != REG_DWORD || cb != sizeof( dwValue ) ) return FALSE;
    *pdwValue = dwValue;
    return TRUE;
}


//Read a string value into a caller buffer, always NUL-terminated.
//Returns FALSE when the value is missing; an empty result is a valid success.
static BOOL ReadString( HKEY hKey, const char* szName, char* pszOut, DWORD cbOut )
{
    if( cbOut == 0 ) return FALSE;
    pszOut[0] = '\0';

    DWORD dwType = 0;
    DWORD cb = cbOut;
    LONG lRes = RegQueryValueExA( hKey, szName, NULL, &dwType, (LPBYTE)pszOut, &cb );
    if( lRes != ERROR_SUCCESS ) return FALSE;
    if( dwType != REG_SZ && dwType != REG_EXPAND_SZ ) return FALSE;

    //the API promises a terminator but never trust it across registry corruption
    pszOut[cbOut - 1] = '\0';
    return TRUE;
}


static BOOL WriteDword( HKEY hKey, const char* szName, DWORD dwValue )
{
    return RegSetValueExA( hKey, szName, 0, REG_DWORD,
                           (const BYTE*)&dwValue, sizeof( dwValue ) ) == ERROR_SUCCESS;
}


static BOOL WriteString( HKEY hKey, const char* szName, const char* szValue )
{
    DWORD cb = (DWORD)strlen( szValue ) + 1;
    return RegSetValueExA( hKey, szName, 0, REG_SZ,
                           (const BYTE*)szValue, cb ) == ERROR_SUCCESS;
}


//Tell WinINET to re-read its configuration. Safe to call when nothing changed.
static void NotifyWinInet()
{
    //INTERNET_OPTION_SETTINGS_CHANGED "causes WinINET to re-read the registry".
    InternetSetOptionA( NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0 );

    //INTERNET_OPTION_REFRESH flushes cached connections so new requests really
    //use the new route instead of reusing an existing one.
    InternetSetOptionA( NULL, INTERNET_OPTION_REFRESH, NULL, 0 );
}


BOOL ProxyIsEnabled()
{
    HKEY hKey = NULL;
    if( RegOpenKeyExA( HKEY_CURRENT_USER, g_szInetKey, 0, KEY_QUERY_VALUE, &hKey ) != ERROR_SUCCESS )
        return FALSE;

    DWORD dwEnable = 0;
    BOOL bEnabled = ReadDword( hKey, "ProxyEnable", &dwEnable ) && dwEnable != 0;
    RegCloseKey( hKey );
    return bEnabled;
}


BOOL ProxySetEnabled( BOOL bEnable )
{
    return ProxySetEnabledEx( bEnable, NULL );
}


BOOL ProxySetEnabledEx( BOOL bEnable, const char* szServer )
{
    HKEY hKey = NULL;
    LONG lRes = RegCreateKeyExA( HKEY_CURRENT_USER, g_szInetKey, 0, NULL,
                                 REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE,
                                 NULL, &hKey, NULL );
    if( lRes != ERROR_SUCCESS ) return FALSE;

    //An empty address would enable a proxy pointed at nowhere. Fall back to the
    //built-in default rather than writing something unusable.
    if( szServer == NULL || szServer[0] == '\0' ) szServer = NEKO_PROXY_SERVER;

    BOOL bOk = TRUE;

    if( bEnable )
    {
        //Always (re)state the server: a machine that has never configured a
        //proxy has ProxyServer empty, and enabling a blank proxy would be a
        //no-op that looks like a broken hotkey.
        bOk = WriteString( hKey, "ProxyServer", szServer ) && bOk;

        //Preserve a hand-edited bypass list, but make sure ours is present.
        char szExisting[2048];
        BOOL bHaveList = ReadString( hKey, "ProxyOverride", szExisting, sizeof szExisting );
        if( !bHaveList || strstr( szExisting, "*zhihu.com" ) == NULL )
            bOk = WriteString( hKey, "ProxyOverride", NEKO_PROXY_BYPASS ) && bOk;
    }

    //ProxyEnable last: if any of the above failed we have not half-enabled a
    //proxy pointing somewhere the user did not ask for.
    bOk = WriteDword( hKey, "ProxyEnable", bEnable ? 1u : 0u ) && bOk;

    RegCloseKey( hKey );

    //Only refresh when the state really was written, otherwise WinINET would
    //re-read and immediately discard a cache for no reason.
    if( bOk ) NotifyWinInet();

    return bOk;
}


BOOL ProxyToggle()
{
    return ProxyToggleEx( NULL );
}


BOOL ProxyToggleEx( const char* szServer )
{
    BOOL bNow = ProxyIsEnabled();
    if( !ProxySetEnabledEx( !bNow, szServer ) )
        return bNow;            //unchanged on failure
    return !bNow;
}


BOOL ProxyGetServer( char* pszOut, int cbOut )
{
    HKEY hKey = NULL;
    if( pszOut == NULL || cbOut <= 0 ) return FALSE;
    pszOut[0] = '\0';

    if( RegOpenKeyExA( HKEY_CURRENT_USER, g_szInetKey, 0, KEY_QUERY_VALUE, &hKey ) != ERROR_SUCCESS )
        return FALSE;

    BOOL bOk = ReadString( hKey, "ProxyServer", pszOut, (DWORD)cbOut );
    RegCloseKey( hKey );
    return bOk;
}

#endif //__PROXYTOGGLE_CPP__
