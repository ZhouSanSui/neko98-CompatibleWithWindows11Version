/************************************
 
    "Start with Windows" support

    Stored in the per-user Run key:
        HKCU\Software\Microsoft\Windows\CurrentVersion\Run
        Neko98 = "C:\...\Neko98.exe"

    Per-user (HKCU) was chosen deliberately over
    HKLM because it needs no administrator rights,
    cannot trip UAC, and lets each user decide
    independently. Windows itself only ever reads
    this value; nothing else has to be installed.

 *************************************/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "AutoStart.h"

static const char* g_szRunKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

/* True when the path ends with NEKO_PET_EXE, compared case-insensitively.
   The comparison is length-bounded on purpose: an unbounded lstrcmpi() from the
   suffix offset would keep comparing past the name and would be wrong the moment
   the input carried anything after it. */
static BOOL HasFileName( const char* pszPath, const char* pszName )
{
    size_t cbPath = strlen( pszPath );
    size_t cbName = strlen( pszName );
    if( cbPath < cbName ) return FALSE;
    return ( _strnicmp( pszPath + ( cbPath - cbName ), pszName, cbName ) == 0 );
}

//Build the value we want stored: the full path of the PET in quotes, so a path
//containing spaces still launches correctly.
//
//IMPORTANT: this code is linked into BOTH executables. The Run entry has to name
//the pet, so when the settings program asks (which is the normal case - the
//checkbox lives there) we must not simply store our own module path, or Windows
//would pop up the settings dialog at every logon instead of the cat. The pet
//sits next to the settings program, so derive its path from our own directory.
static BOOL BuildRunValue( char* pszOut, int cbOut )
{
    char szPath[MAX_PATH];
    DWORD dw = GetModuleFileName( NULL, szPath, MAX_PATH );
    if( dw == 0 || dw >= MAX_PATH ) return FALSE;

    //If we ARE the pet, our own path is already the right answer.
    if( !HasFileName( szPath, NEKO_PET_EXE ) )
    {
        char* pszSlash = strrchr( szPath, '\\' );
        if( pszSlash == NULL ) return FALSE;

        //Truncate to just the directory (keeping the trailing backslash) and
        //append the pet's file name.
        *(pszSlash + 1) = '\0';
        if( strlen( szPath ) + strlen( NEKO_PET_EXE ) >= MAX_PATH ) return FALSE;
        lstrcat( szPath, NEKO_PET_EXE );

        //The sibling has to be really there. If it is not, the installation is
        //incomplete, and registering OUR OWN path would make Windows open the
        //settings dialog at every logon instead of the cat. Failing here makes
        //the checkbox show its warning, which is the honest outcome.
        DWORD dwAttr = GetFileAttributes( szPath );
        if( dwAttr == INVALID_FILE_ATTRIBUTES || ( dwAttr & FILE_ATTRIBUTE_DIRECTORY ) )
            return FALSE;
    }

    //GetModuleFileName() returns the path that was used to launch us. If we were
    //started through a relative path or a short (8.3) name, persist the fully
    //resolved long path so Explorer can always find us after a reboot.
    char szFull[MAX_PATH];
    DWORD dwFull = GetLongPathName( szPath, szFull, MAX_PATH );
    if( dwFull > 0 && dwFull < MAX_PATH ) lstrcpyn( szPath, szFull, MAX_PATH );

    wsprintf( pszOut, "\"%s\"", szPath );
    return ( (int)strlen( pszOut ) < cbOut - 1 );
}

BOOL NekoAutoStartIsEnabled()
{
    HKEY hKey = NULL;
    if( RegOpenKeyEx( HKEY_CURRENT_USER, g_szRunKey, 0, KEY_READ, &hKey ) != ERROR_SUCCESS )
        return FALSE;

    char szStored[MAX_PATH * 2];
    DWORD dwSize = sizeof(szStored);
    DWORD dwType = 0;
    LONG lRes = RegQueryValueEx( hKey, NEKO_RUN_VALUE, NULL, &dwType, (LPBYTE)szStored, &dwSize );
    RegCloseKey( hKey );

    if( lRes != ERROR_SUCCESS || dwType != REG_SZ ) return FALSE;

    //An entry only counts as "enabled" if it still points at this executable:
    //a stale path means the user moved the program and it would not start.
    char szWant[MAX_PATH * 2];
    if( !BuildRunValue( szWant, sizeof(szWant) ) ) return FALSE;

    return ( lstrcmpi( szStored, szWant ) == 0 );
}

BOOL NekoAutoStartSet( BOOL bEnable )
{
    HKEY hKey = NULL;
    LONG lRes = RegCreateKeyEx( HKEY_CURRENT_USER, g_szRunKey, 0, NULL,
                                REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                                NULL, &hKey, NULL );
    if( lRes != ERROR_SUCCESS ) return FALSE;

    BOOL bOk = FALSE;
    if( bEnable )
    {
        char szValue[MAX_PATH * 2];
        if( BuildRunValue( szValue, sizeof(szValue) ) )
        {
            lRes = RegSetValueEx( hKey, NEKO_RUN_VALUE, 0, REG_SZ,
                                  (const BYTE*)szValue, (DWORD)strlen( szValue ) + 1 );
            bOk = ( lRes == ERROR_SUCCESS );
        }
    }
    else
    {
        lRes = RegDeleteValue( hKey, NEKO_RUN_VALUE );
        //already absent is just as good as deleted
        bOk = ( lRes == ERROR_SUCCESS || lRes == ERROR_FILE_NOT_FOUND );
    }

    RegCloseKey( hKey );
    return bOk;
}

BOOL NekoAutoStartRepair()
{
    HKEY hKey = NULL;
    if( RegOpenKeyEx( HKEY_CURRENT_USER, g_szRunKey, 0, KEY_READ, &hKey ) != ERROR_SUCCESS )
        return FALSE;

    char szStored[MAX_PATH * 2];
    DWORD dwSize = sizeof(szStored);
    DWORD dwType = 0;
    LONG lRes = RegQueryValueEx( hKey, NEKO_RUN_VALUE, NULL, &dwType, (LPBYTE)szStored, &dwSize );
    RegCloseKey( hKey );

    //Nothing registered: the user has not asked for auto-start, leave it alone.
    if( lRes != ERROR_SUCCESS ) return FALSE;

    char szWant[MAX_PATH * 2];
    if( !BuildRunValue( szWant, sizeof(szWant) ) ) return FALSE;

    //Already correct, or the user pointed it somewhere on purpose.
    if( lstrcmpi( szStored, szWant ) == 0 ) return FALSE;

    return NekoAutoStartSet( TRUE );
}
