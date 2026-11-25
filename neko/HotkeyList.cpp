///////////////////////////////////////////////////////////////////////////////
// HotkeyList.cpp - see HotkeyList.h for the storage format and the rationale.
///////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "HotkeyList.h"
#include "NekoSettings.h"
#include "NekoCommon.h"

NEKOHOTKEY g_HotkeyList[NEKO_HT_MAX];
int        g_nHotkeyList = 0;

static const char* g_szListKey = "HotkeyList";

//Default parameters for a new proxy entry, as the user asked for it.
#define NEKO_HT_DEF_PROXY_HOST "192.168.1.1"
#define NEKO_HT_DEF_PROXY_PORT "7890"

//Projection modes, in the order the two combo boxes offer them. The index into
//this table is what gets persisted, so the strings can be reworded later
//without invalidating stored entries - but do not reorder the table.
static const char* g_szProjMode[4] = { "屏幕1", "屏幕2", "扩展", "辅助" };
static const char* g_szProjSwitch[4] = { "/internal", "/external", "/extend", "/clone" };


const char* HotkeyTypeName( DWORD dwType )
{
    switch( dwType )
    {
        case NEKO_HT_HIDE:       return "隐藏/缩放";
        case NEKO_HT_PROXY:      return "代理";
        case NEKO_HT_NOSLEEP:    return "禁止自动休眠";
        case NEKO_HT_WEBSITE:    return "打开网站";
        case NEKO_HT_PROJECTION: return "投影方式";
    }
    return "未知";
}


/* Projection helpers - shared with the dispatcher so the two can never disagree
   about which mode maps to which DisplaySwitch argument. */
const char* HotkeyProjectionMode( int nIndex )
{
    if( nIndex < 0 || nIndex > 3 ) return g_szProjMode[0];
    return g_szProjMode[nIndex];
}

const char* HotkeyProjectionSwitch( int nIndex )
{
    if( nIndex < 0 || nIndex > 3 ) return g_szProjSwitch[0];
    return g_szProjSwitch[nIndex];
}

/* Turn a stored mode name back into an index; -1 when it is not one of ours. */
int HotkeyProjectionIndex( const char* szMode )
{
    int i;
    if( szMode == NULL ) return -1;
    for( i = 0; i < 4; i++ )
        if( strcmp( szMode, g_szProjMode[i] ) == 0 ) return i;
    return -1;
}


void HotkeyListSetDefaults( LPNEKOHOTKEY lpEntry )
{
    if( lpEntry == NULL ) return;

    lpEntry->szP1[0] = '\0';
    lpEntry->szP2[0] = '\0';

    switch( lpEntry->dwType )
    {
        case NEKO_HT_PROXY:
            //The user asked for this exact default rather than the address this
            //machine happens to use, so a freshly added entry is predictable.
            strcpy( lpEntry->szP1, NEKO_HT_DEF_PROXY_HOST );
            strcpy( lpEntry->szP2, NEKO_HT_DEF_PROXY_PORT );
            break;

        case NEKO_HT_WEBSITE:
            strcpy( lpEntry->szP1, "https://www.bing.com" );
            strcpy( lpEntry->szP2, "edge" );
            break;

        case NEKO_HT_PROJECTION:
            //A freshly added pair must differ, or the switch would be a no-op.
            strcpy( lpEntry->szP1, g_szProjMode[2] );   //扩展
            strcpy( lpEntry->szP2, g_szProjMode[0] );   //屏幕1
            break;

        case NEKO_HT_HIDE:
        case NEKO_HT_NOSLEEP:
        default:
            break;
    }
}


void HotkeyListLabel( const NEKOHOTKEY* lpEntry, char* szOut, int cbOut )
{
    if( lpEntry == NULL || szOut == NULL || cbOut <= 0 ) return;
    szOut[0] = '\0';

    switch( lpEntry->dwType )
    {
        case NEKO_HT_PROXY:
        {
            char szTmp[64];
            _snprintf( szTmp, sizeof szTmp, "%s:%s", lpEntry->szP1, lpEntry->szP2 );
            szTmp[sizeof szTmp - 1] = '\0';
            _snprintf( szOut, cbOut, "代理 %s", szTmp );
            break;
        }

        case NEKO_HT_WEBSITE:
        {
            //Only the host is interesting in a narrow list column; the full URL
            //is shown in the entry's own edit box when it is expanded.
            char szHost[64];
            const char* p = lpEntry->szP1;
            int n = 0;
            if( strncmp( p, "https://", 8 ) == 0 ) p += 8;
            else if( strncmp( p, "http://", 7 ) == 0 ) p += 7;
            while( *p != '\0' && *p != '/' && n < (int)sizeof szHost - 1 )
                szHost[n++] = *p++;
            szHost[n] = '\0';
            _snprintf( szOut, cbOut, "打开网站 %s", szHost );
            break;
        }

        case NEKO_HT_PROJECTION:
            //Plain ASCII arrow: the GBK code page the build targets has no
            //double-arrow glyph, and a missing glyph would print as garbage.
            _snprintf( szOut, cbOut, "投影 %s <-> %s", lpEntry->szP1, lpEntry->szP2 );
            break;

        default:
            _snprintf( szOut, cbOut, "%s", HotkeyTypeName( lpEntry->dwType ) );
            break;
    }

    szOut[cbOut - 1] = '\0';
}


int HotkeyListAppend( DWORD dwType )
{
    int n;
    if( g_nHotkeyList >= NEKO_HT_MAX ) return -1;

    n = g_nHotkeyList;
    memset( &g_HotkeyList[n], 0, sizeof( NEKOHOTKEY ) );
    g_HotkeyList[n].dwType   = dwType;
    g_HotkeyList[n].dwHotkey = 0;         //the user picks it; empty = disabled
    HotkeyListSetDefaults( &g_HotkeyList[n] );

    g_nHotkeyList++;
    return n;
}


BOOL HotkeyListRemove( int nIndex )
{
    int i;
    if( nIndex < 0 || nIndex >= g_nHotkeyList ) return FALSE;

    for( i = nIndex; i + 1 < g_nHotkeyList; i++ )
        g_HotkeyList[i] = g_HotkeyList[i + 1];

    g_nHotkeyList--;
    memset( &g_HotkeyList[g_nHotkeyList], 0, sizeof( NEKOHOTKEY ) );
    return TRUE;
}


BOOL HotkeyListValidate( char* szError, int cbError )
{
    int i, j;

    for( i = 0; i < g_nHotkeyList; i++ )
    {
        if( g_HotkeyList[i].dwHotkey == 0 ) continue;

        BYTE bVk  = (BYTE)( g_HotkeyList[i].dwHotkey & 0xFF );
        BYTE bMod = (BYTE)( ( g_HotkeyList[i].dwHotkey >> 8 ) & 0xFF );

        if( bVk == 0 || ( bMod & ( HOTKEYF_CONTROL_ | HOTKEYF_ALT_ | HOTKEYF_SHIFT_ | HOTKEYF_EXT_ ) ) == 0 )
        {
            if( szError ) _snprintf( szError, cbError,
                "“%s”的快捷键无效：请至少使用 Ctrl、Alt、Shift 或 Win 中的一个，再配合一个普通按键。",
                HotkeyTypeName( g_HotkeyList[i].dwType ) );
            if( szError ) szError[cbError - 1] = '\0';
            return FALSE;
        }

        for( j = i + 1; j < g_nHotkeyList; j++ )
        {
            if( g_HotkeyList[j].dwHotkey == 0 ) continue;
            if( g_HotkeyList[j].dwHotkey != g_HotkeyList[i].dwHotkey ) continue;

            if( szError ) _snprintf( szError, cbError,
                "“%s”和“%s”不能使用同一个快捷键，请修改其中一个。",
                HotkeyTypeName( g_HotkeyList[i].dwType ),
                HotkeyTypeName( g_HotkeyList[j].dwType ) );
            if( szError ) szError[cbError - 1] = '\0';
            return FALSE;
        }
    }

    //Projection pairs are the one kind whose parameters can be self-inconsistent:
    //switching to the mode you are already in would do nothing visible.
    for( i = 0; i < g_nHotkeyList; i++ )
    {
        if( g_HotkeyList[i].dwType != NEKO_HT_PROJECTION ) continue;
        if( strcmp( g_HotkeyList[i].szP1, g_HotkeyList[i].szP2 ) == 0 )
        {
            if( szError ) _snprintf( szError, cbError,
                "投影方式的两个选项不能相同，否则按快捷键不会有任何变化。" );
            if( szError ) szError[cbError - 1] = '\0';
            return FALSE;
        }
    }

    return TRUE;
}


/* Strip anything that would break the line format. A TAB or a line break inside
   a parameter would silently truncate the entry on the next load, which is the
   kind of corruption that is very hard to diagnose later. */
static void SanitiseField( const char* szIn, char* szOut, int cbOut )
{
    int n = 0;
    if( szOut == NULL || cbOut <= 0 ) return;
    szOut[0] = '\0';
    if( szIn == NULL ) return;

    while( *szIn != '\0' && n < cbOut - 1 )
    {
        char ch = *szIn++;
        if( ch == '\t' || ch == '\r' || ch == '\n' )
            ch = ' ';                     //keep the visual gap, lose the format
        szOut[n++] = ch;
    }
    szOut[n] = '\0';
}


int HotkeyListLoad()
{
    CNekoSettings settings( szNekoRegKey, FALSE );
    char szRaw[8192];
    char szLine[2048];
    int  n = 0;

    g_nHotkeyList = 0;
    memset( g_HotkeyList, 0, sizeof( g_HotkeyList ) );

    szRaw[0] = '\0';
    if( settings.IsOpen() )
        settings.GetString( g_szListKey, szRaw, sizeof szRaw );

    if( szRaw[0] != '\0' )
    {
        const char* p = szRaw;
        while( *p != '\0' && n < NEKO_HT_MAX )
        {
            int k = 0;

            //copy one line
            while( *p != '\0' && *p != '\n' && k < (int)sizeof szLine - 1 )
            {
                if( *p != '\r' ) szLine[k++] = *p;
                p++;
            }
            szLine[k] = '\0';
            if( *p == '\n' ) p++;

            if( szLine[0] == '\0' ) continue;

            //split into at most four TAB-separated fields
            {
                char* aField[4];
                int   nField = 0;
                char* q = szLine;

                aField[nField++] = q;
                while( *q != '\0' && nField < 4 )
                {
                    if( *q == '\t' )
                    {
                        *q = '\0';
                        aField[nField++] = q + 1;
                    }
                    q++;
                }

                if( nField >= 2 )
                {
                    DWORD dwType = (DWORD)strtoul( aField[0], NULL, 10 );
                    if( dwType < NEKO_HT_TYPES )
                    {
                        LPNEKOHOTKEY lp = &g_HotkeyList[n];
                        memset( lp, 0, sizeof( NEKOHOTKEY ) );
                        lp->dwType   = dwType;
                        lp->dwHotkey = (DWORD)strtoul( aField[1], NULL, 16 );
                        if( nField >= 3 ) SanitiseField( aField[2], lp->szP1, sizeof lp->szP1 );
                        if( nField >= 4 ) SanitiseField( aField[3], lp->szP2, sizeof lp->szP2 );
                        n++;
                    }
                }
            }
        }
    }

    g_nHotkeyList = n;

    //First run of this build: carry the two legacy combinations over so the user
    //does not lose the keys they already learned. Only when the list is empty,
    //otherwise a deliberate "no shortcuts" would be overwritten every launch.
    if( n == 0 && !settings.GetString( g_szListKey, szRaw, sizeof szRaw ) )
    {
        DWORD dwLegacyHide  = 0;
        DWORD dwLegacyProxy = 0;
        BOOL  bHaveHide     = FALSE;
        BOOL  bHaveProxy    = FALSE;

        if( settings.IsOpen() )
        {
            bHaveHide  = settings.GetInt( szNekoHideHotkeyKey, &dwLegacyHide );
            bHaveProxy = settings.GetInt( szNekoHotkeyKey, &dwLegacyProxy );
        }

        if( bHaveHide && dwLegacyHide != 0 )
        {
            int i = HotkeyListAppend( NEKO_HT_HIDE );
            if( i >= 0 ) g_HotkeyList[i].dwHotkey = dwLegacyHide;
        }
        if( bHaveProxy && dwLegacyProxy != 0 )
        {
            int i = HotkeyListAppend( NEKO_HT_PROXY );
            if( i >= 0 )
            {
                g_HotkeyList[i].dwHotkey = dwLegacyProxy;
                strcpy( g_HotkeyList[i].szP1, NEKO_PROXY_MIGRATED_HOST );
                strcpy( g_HotkeyList[i].szP2, NEKO_PROXY_MIGRATED_PORT );
            }
        }

        //Nothing stored at all is a genuinely fresh install: give the user the
        //two defaults the previous build shipped, so the feature is discoverable.
        if( g_nHotkeyList == 0 )
        {
            int i = HotkeyListAppend( NEKO_HT_HIDE );
            if( i >= 0 ) g_HotkeyList[i].dwHotkey = NEKO_DEFAULT_HIDE_HOTKEY;
            i = HotkeyListAppend( NEKO_HT_PROXY );
            if( i >= 0 )
            {
                g_HotkeyList[i].dwHotkey = NEKO_DEFAULT_HOTKEY;
                strcpy( g_HotkeyList[i].szP1, NEKO_PROXY_MIGRATED_HOST );
                strcpy( g_HotkeyList[i].szP2, NEKO_PROXY_MIGRATED_PORT );
            }
        }
    }

    return g_nHotkeyList;
}


BOOL HotkeyListSave()
{
    CNekoSettings settings( szNekoRegKey, TRUE );
    char szRaw[8192];
    int  i;
    int  nUsed = 0;

    if( !settings.IsOpen() ) return FALSE;

    szRaw[0] = '\0';

    for( i = 0; i < g_nHotkeyList; i++ )
    {
        char szP1[NEKO_HT_P1];
        char szP2[NEKO_HT_P2];
        char szLine[2048];

        SanitiseField( g_HotkeyList[i].szP1, szP1, sizeof szP1 );
        SanitiseField( g_HotkeyList[i].szP2, szP2, sizeof szP2 );

        _snprintf( szLine, sizeof szLine, "%lu\t%08lX\t%s\t%s\n",
                   (unsigned long)g_HotkeyList[i].dwType,
                   (unsigned long)g_HotkeyList[i].dwHotkey,
                   szP1, szP2 );
        szLine[sizeof szLine - 1] = '\0';

        if( strlen( szRaw ) + strlen( szLine ) + 1 >= sizeof szRaw )
            break;                        //never write a truncated list

        strcat( szRaw, szLine );
        nUsed++;
    }

    //An empty list must still be written, otherwise the loader cannot tell
    //"the user deleted everything" from "this build never ran".
    return settings.SetString( (LPSTR)g_szListKey, szRaw );
}
