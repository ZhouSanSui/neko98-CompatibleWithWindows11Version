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

/* Strip the surrounding quotes from a stored Run value, so the path can be
   handed to GetFileAttributes. Returns FALSE for an empty value. */
static BOOL UnquotePath( const char* pszValue, char* pszOut, int cbOut )
{
    if( pszValue == NULL || *pszValue == '\0' ) return FALSE;

    const char* pszRead = pszValue;
    if( *pszRead == '"' ) pszRead++;

    int i = 0;
    while( *pszRead != '\0' && *pszRead != '"' && i < cbOut - 1 )
        pszOut[i++] = *pszRead++;
    pszOut[i] = '\0';

    return ( i > 0 );
}

/* TRUE when the path names a file that is really there (not a directory). */
static BOOL FileExists( const char* pszPath )
{
    DWORD dwAttr = GetFileAttributes( pszPath );
    return ( dwAttr != INVALID_FILE_ATTRIBUTES && ( dwAttr & FILE_ATTRIBUTE_DIRECTORY ) == 0 );
}

/* TRUE when we are running from the throw-away folder our self-extracting
   installer unpacks to. 7-Zip extracts the payload to "%TEMP%\7z<random>.tmp"
   and DELETES it again when the program exits, so storing a Run entry that
   points in there would leave auto-start dead by the next logon.

   The test is deliberately narrow (the leaf folder must look like a 7-Zip
   extraction folder) because the pet itself is perfectly happy living in any
   ordinary folder, including a portable one the user unzipped by hand. */
static BOOL IsThrowAwayDir( const char* pszModulePath )
{
    char szDir[MAX_PATH];
    lstrcpyn( szDir, pszModulePath, MAX_PATH );

    char* pszSlash = strrchr( szDir, '\\' );
    if( pszSlash != NULL ) *pszSlash = '\0';

    //the leaf folder name, e.g. "7zS3A2F.tmp"
    char* pszLeaf = strrchr( szDir, '\\' );
    pszLeaf = ( pszLeaf != NULL ) ? ( pszLeaf + 1 ) : szDir;

    size_t cbLeaf = strlen( pszLeaf );
    if( cbLeaf < 6 ) return FALSE;
    if( _strnicmp( pszLeaf, "7z", 2 ) != 0 ) return FALSE;
    if( _stricmp( pszLeaf + ( cbLeaf - 4 ), ".tmp" ) != 0 ) return FALSE;

    return TRUE;
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

    //Only ever touch a plain string; anything else is not ours to interpret.
    if( dwType != REG_SZ ) return FALSE;

    //Do not adopt the folder we happen to be running from when that folder is
    //a throw-away extraction directory: the Run entry would name a path that
    //Windows deletes when this process exits, so auto-start would be dead by
    //the next logon. The user can still enable it from an installed copy.
    char szSelf[MAX_PATH];
    DWORD dwSelf = GetModuleFileName( NULL, szSelf, MAX_PATH );
    if( dwSelf == 0 || dwSelf >= MAX_PATH ) return FALSE;
    if( IsThrowAwayDir( szSelf ) ) return FALSE;

    char szWant[MAX_PATH * 2];
    if( !BuildRunValue( szWant, sizeof(szWant) ) ) return FALSE;

    //Already correct: nothing to do.
    if( lstrcmpi( szStored, szWant ) == 0 ) return FALSE;

    /* Decide whether the stored entry is actually BROKEN.

       Repair exists to rescue an entry that went stale because the user moved
       the folder. It must NOT repoint an entry that still resolves to a real
       pet file: that would mean launching a copy from one folder silently
       steals the auto-start entry from the copy the user installed, which is
       exactly what happened when a test build was run from a scratch folder.
       The one exception is a value that names the settings program, which is
       never a valid auto-start target and is migrated. */
    char szExisting[MAX_PATH];
    if( UnquotePath( szStored, szExisting, sizeof(szExisting) ) )
    {
        if( FileExists( szExisting ) && !HasFileName( szExisting, NEKO_SETTINGS_EXE ) )
            return FALSE;
    }

    return NekoAutoStartSet( TRUE );
}
