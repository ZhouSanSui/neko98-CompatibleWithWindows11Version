///////////////////////////////////////////////////////////////////////////////
// test_proxy.cpp — runtime test for ProxyToggle.cpp (the proxy hot key feature).
//
// Drives the REAL ProxyToggle.o, so it exercises the code the shipped program
// runs rather than a copy of it.
//
// The test is deliberately non-destructive: it snapshots ProxyEnable,
// ProxyServer and ProxyOverride before touching anything and puts all three back
// afterwards, so running it never changes the user's browsing.
//
// Build: see build_test_proxy.bat
///////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "ProxyToggle.h"

#define RUN_KEY     "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"

static int g_nFail = 0;

static void CHECK( const char* szWhat, BOOL bOk, const char* szDetail = "" )
{
    printf( "[%s] %s%s%s\n", bOk ? "PASS" : "FAIL", szWhat,
            szDetail[0] ? "  " : "", szDetail );
    if( !bOk ) g_nFail++;
}

//raw registry access, so the test can plant values ProxyToggle did not write
static BOOL RawGetDword( const char* szName, DWORD* pdw )
{
    HKEY hKey = NULL;
    if( RegOpenKeyExA( HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &hKey ) != ERROR_SUCCESS )
        return FALSE;

    DWORD dwType = 0, cb = sizeof( DWORD );
    LONG lRes = RegQueryValueExA( hKey, szName, NULL, &dwType, (LPBYTE)pdw, &cb );
    RegCloseKey( hKey );
    return lRes == ERROR_SUCCESS && dwType == REG_DWORD;
}

static BOOL RawGetString( const char* szName, char* pszOut, DWORD cbOut )
{
    pszOut[0] = '\0';

    HKEY hKey = NULL;
    if( RegOpenKeyExA( HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &hKey ) != ERROR_SUCCESS )
        return FALSE;

    DWORD cb = cbOut;
    LONG lRes = RegQueryValueExA( hKey, szName, NULL, NULL, (LPBYTE)pszOut, &cb );
    RegCloseKey( hKey );
    pszOut[cbOut - 1] = '\0';
    return lRes == ERROR_SUCCESS;
}

static BOOL RawSetDword( const char* szName, DWORD dwValue )
{
    HKEY hKey = NULL;
    if( RegCreateKeyExA( HKEY_CURRENT_USER, RUN_KEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_SET_VALUE, NULL, &hKey, NULL ) != ERROR_SUCCESS )
        return FALSE;

    LONG lRes = RegSetValueExA( hKey, szName, 0, REG_DWORD, (const BYTE*)&dwValue, sizeof dwValue );
    RegCloseKey( hKey );
    return lRes == ERROR_SUCCESS;
}


int main()
{
    printf( "=== ProxyToggle runtime test ===\n\n" );

    //--------------------------------------------------------------------
    // 0. snapshot the user's real settings so they can be restored
    //--------------------------------------------------------------------
    DWORD dwOrigEnable = 0;
    BOOL  bHaveEnable  = RawGetDword( "ProxyEnable", &dwOrigEnable );

    char szOrigServer[1024];
    if( !RawGetString( "ProxyServer", szOrigServer, sizeof szOrigServer ) )
        szOrigServer[0] = '\0';

    static char szOrigOverride[4096];
    if( !RawGetString( "ProxyOverride", szOrigOverride, sizeof szOrigOverride ) )
        szOrigOverride[0] = '\0';

    printf( "original ProxyEnable=%lu server=[%s]\n", dwOrigEnable, szOrigServer );
    printf( "original bypass=%u chars\n\n", (unsigned)strlen( szOrigOverride ) );

    //--------------------------------------------------------------------
    // 1. disabled -> ProxyIsEnabled() must report FALSE
    //--------------------------------------------------------------------
    RawSetDword( "ProxyEnable", 0 );
    CHECK( "a disabled proxy reads as disabled", ProxyIsEnabled() == FALSE );

    //--------------------------------------------------------------------
    // 2. enabling writes the server, the bypass list and the flag
    //--------------------------------------------------------------------
    BOOL bOk = ProxySetEnabled( TRUE );
    CHECK( "ProxySetEnabled(TRUE) succeeds", bOk );

    CHECK( "ProxyIsEnabled() reports enabled", ProxyIsEnabled() == TRUE );

    char szServer[1024];
    RawGetString( "ProxyServer", szServer, sizeof szServer );
    CHECK( "the configured server was written",
           lstrcmpiA( szServer, NEKO_PROXY_SERVER ) == 0, szServer );

    static char szBypass[4096];
    RawGetString( "ProxyOverride", szBypass, sizeof szBypass );
    printf( "bypass now = %s\n", szBypass );

    CHECK( "the bypass list contains *zhihu.com", strstr( szBypass, "*zhihu.com" ) != NULL );
    CHECK( "the bypass list contains *zhimg.com", strstr( szBypass, "*zhimg.com" ) != NULL );
    CHECK( "the bypass list contains *jd.com", strstr( szBypass, "*jd.com" ) != NULL );
    CHECK( "the bypass list contains *360buyimg.com", strstr( szBypass, "*360buyimg.com" ) != NULL );
    CHECK( "the bypass list contains 100ime-iat-api.xfyun.cn",
           strstr( szBypass, "100ime-iat-api.xfyun.cn" ) != NULL );
    CHECK( "the bypass list contains *.zhousansui.top",
           strstr( szBypass, "*.zhousansui.top" ) != NULL );
    CHECK( "the bypass list contains the 172.2* wildcard entry",
           strstr( szBypass, "172.2*" ) != NULL );
    CHECK( "the bypass list contains <local> (do not proxy local addresses)",
           strstr( szBypass, "<local>" ) != NULL );

    //--------------------------------------------------------------------
    // 3. a hand-edited bypass list is preserved
    //--------------------------------------------------------------------
    RawSetDword( "ProxyEnable", 0 );
    HKEY hKey = NULL;
    if( RegCreateKeyExA( HKEY_CURRENT_USER, RUN_KEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_SET_VALUE, NULL, &hKey, NULL ) == ERROR_SUCCESS )
    {
        const char* szMine = "*zhihu.com;my-own-intranet.example;<local>";
        RegSetValueExA( hKey, "ProxyOverride", 0, REG_SZ,
                        (const BYTE*)szMine, (DWORD)strlen( szMine ) + 1 );
        RegCloseKey( hKey );
    }

    ProxySetEnabled( TRUE );
    RawGetString( "ProxyOverride", szBypass, sizeof szBypass );
    CHECK( "an existing hand-edited bypass list is not overwritten",
           strstr( szBypass, "my-own-intranet.example" ) != NULL, szBypass );

    //--------------------------------------------------------------------
    // 4. a machine with no proxy configured at all gets the full list
    //--------------------------------------------------------------------
    RawSetDword( "ProxyEnable", 0 );
    hKey = NULL;
    if( RegCreateKeyExA( HKEY_CURRENT_USER, RUN_KEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_SET_VALUE, NULL, &hKey, NULL ) == ERROR_SUCCESS )
    {
        RegDeleteValueA( hKey, "ProxyOverride" );
        RegCloseKey( hKey );
    }

    ProxySetEnabled( TRUE );
    RawGetString( "ProxyOverride", szBypass, sizeof szBypass );
    CHECK( "a blank machine gets the full bypass list",
           strstr( szBypass, "*zhihu.com" ) != NULL &&
           strstr( szBypass, "<local>" )    != NULL );

    //--------------------------------------------------------------------
    // 5. disabling clears the flag but keeps the address
    //--------------------------------------------------------------------
    bOk = ProxySetEnabled( FALSE );
    CHECK( "ProxySetEnabled(FALSE) succeeds", bOk );
    CHECK( "ProxyIsEnabled() reports disabled", ProxyIsEnabled() == FALSE );

    char szAfter[1024];
    RawGetString( "ProxyServer", szAfter, sizeof szAfter );
    CHECK( "disabling keeps the proxy address for next time",
           lstrcmpiA( szAfter, NEKO_PROXY_SERVER ) == 0, szAfter );

    //--------------------------------------------------------------------
    // 6. ProxyToggle() alternates and reports the state afterwards
    //--------------------------------------------------------------------
    ProxySetEnabled( FALSE );
    BOOL bAfter = ProxyToggle();
    CHECK( "ProxyToggle() turns it on and reports on", bAfter == TRUE && ProxyIsEnabled() == TRUE );

    bAfter = ProxyToggle();
    CHECK( "ProxyToggle() turns it off and reports off", bAfter == FALSE && ProxyIsEnabled() == FALSE );

    //--------------------------------------------------------------------
    // restore whatever the user had
    //--------------------------------------------------------------------
    RawSetDword( "ProxyEnable", bHaveEnable ? dwOrigEnable : 0 );

    hKey = NULL;
    if( RegCreateKeyExA( HKEY_CURRENT_USER, RUN_KEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_SET_VALUE, NULL, &hKey, NULL ) == ERROR_SUCCESS )
    {
        RegSetValueExA( hKey, "ProxyServer", 0, REG_SZ,
                        (const BYTE*)szOrigServer, (DWORD)strlen( szOrigServer ) + 1 );
        RegSetValueExA( hKey, "ProxyOverride", 0, REG_SZ,
                        (const BYTE*)szOrigOverride, (DWORD)strlen( szOrigOverride ) + 1 );
        RegCloseKey( hKey );
    }

    DWORD dwBack = 0;
    if( RawGetDword( "ProxyEnable", &dwBack ) )
        CHECK( "the user's original proxy state was restored", dwBack == dwOrigEnable );

    printf( "\n=== %s (%d failures) ===\n", g_nFail ? "FAILED" : "ALL PASS", g_nFail );
    return g_nFail ? 2 : 0;
}
