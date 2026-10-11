/************************************
 
    Global hotkey support

 *************************************/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "Hotkey.h"
#include "NekoSettings.h"
#include "NekoCommon.h"

//The combinations currently registered, so we can release exactly them. Index 0
//is the pause/resume key, index 1 the hide/show key.
static DWORD g_dwRegistered[2] = { 0, 0 };

/* Map a slot selector to the pieces we need ********************************************/
static int   SlotOf( BOOL bHide ) { return bHide ? 1 : 0; }
static UINT  IdOf( BOOL bHide )   { return bHide ? NEKO_HIDE_HOTKEY_ID : NEKO_HOTKEY_ID; }
static LPCSTR KeyNameOf( BOOL bHide ) { return bHide ? szNekoHideHotkeyKey : szNekoHotkeyKey; }
static DWORD DefaultOf( BOOL bHide )   { return bHide ? NEKO_DEFAULT_HIDE_HOTKEY : NEKO_DEFAULT_HOTKEY; }

DWORD NekoHotkeyLoad( BOOL bHide )
{
    CNekoSettings settings( szNekoRegKey, FALSE );
    if( !settings.IsOpen() ) return DefaultOf( bHide );

    DWORD dwValue = 0;
    if( !settings.GetInt( KeyNameOf( bHide ), &dwValue ) )
    {
        //No value stored: first run, or the user never changed it. Fall back to
        //the built-in default so the feature is discoverable.
        return DefaultOf( bHide );
    }

    //A stored value is honoured as-is, including 0 (= deliberately disabled).
    return dwValue;
}

BOOL NekoHotkeySave( DWORD dwHotkey, BOOL bHide )
{
    CNekoSettings settings( szNekoRegKey, TRUE );
    if( !settings.IsOpen() ) return FALSE;
    return settings.SetInt( KeyNameOf( bHide ), dwHotkey );
}

void NekoHotkeyRelease( HWND hWnd, BOOL bHide )
{
    if( hWnd != NULL && g_dwRegistered[SlotOf(bHide)] != 0 )
        UnregisterHotKey( hWnd, IdOf( bHide ) );

    g_dwRegistered[SlotOf(bHide)] = 0;
}

void NekoHotkeyReleaseAll( HWND hWnd )
{
    NekoHotkeyRelease( hWnd, FALSE );
    NekoHotkeyRelease( hWnd, TRUE );
}

BOOL NekoHotkeyApply( HWND hWnd, DWORD dwHotkey, BOOL bHide )
{
    if( hWnd == NULL ) return FALSE;

    //Drop whatever we held before, so re-applying is always safe.
    NekoHotkeyRelease( hWnd, bHide );

    if( dwHotkey == 0 ) return FALSE;          //user disabled it
    if( !NekoHotkeyIsUsable( dwHotkey ) ) return FALSE;

    //The two slots must not collide: the system registers by combination, so the
    //second attempt would fail and leave one action unreachable.
    if( NekoHotkeyConflict( dwHotkey, g_dwRegistered[SlotOf(!bHide)] ) ) return FALSE;

    BYTE bVk  = (BYTE)( dwHotkey & 0xFF );
    BYTE bMod = (BYTE)( (dwHotkey >> 8) & 0xFF );

    UINT uMod = 0;
    if( bMod & HOTKEYF_CONTROL_ ) uMod |= MOD_CONTROL;
    if( bMod & HOTKEYF_ALT_ )     uMod |= MOD_ALT;
    if( bMod & HOTKEYF_SHIFT_ )   uMod |= MOD_SHIFT;
    if( bMod & HOTKEYF_EXT_ )     uMod |= MOD_WIN;

    if( !RegisterHotKey( hWnd, IdOf(bHide), uMod | MOD_NOREPEAT, bVk ) )
    {
        //Most likely another program already owns this combination. Try once
        //more without MOD_NOREPEAT, because a few older drivers reject it.
        if( !RegisterHotKey( hWnd, IdOf(bHide), uMod, bVk ) )
            return FALSE;
    }

    g_dwRegistered[SlotOf(bHide)] = dwHotkey;
    return TRUE;
}

DWORD NekoHotkeyCurrent( BOOL bHide )
{
    return g_dwRegistered[SlotOf(bHide)];
}

/* Report the key name in the user's own keyboard layout, so a German or French
   user sees the key that is really printed on their keyboard. */
static void GetKeyName( BYTE bVk, char* pszOut, int cbOut )
{
    if( pszOut == NULL || cbOut <= 0 ) return;
    pszOut[0] = '\0';

    UINT uScan = MapVirtualKey( bVk, MAPVK_VK_TO_VSC );

    //Extended keys need the flag or their name comes back as a number.
    LONG lParam = (LONG)uScan << 16;
    switch( bVk )
    {
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR:  case VK_NEXT:   case VK_LEFT: case VK_RIGHT:
        case VK_UP:     case VK_DOWN:   case VK_NUMLOCK: case VK_DIVIDE:
        case VK_RCONTROL: case VK_RMENU:
            lParam |= (1L << 24);
            break;
    }

    if( GetKeyNameText( lParam, pszOut, cbOut ) == 0 )
    {
        //No layout name available (rare): fall back to a printable character or
        //A-Z, which covers virtually every realistic choice.
        if( ( bVk >= 'A' && bVk <= 'Z' ) || ( bVk >= '0' && bVk <= '9' ) )
            _snprintf( pszOut, cbOut, "%c", (char)bVk );
        else
            _snprintf( pszOut, cbOut, "0x%02X", bVk );
        pszOut[cbOut - 1] = '\0';
    }
}

void NekoHotkeyFormat( DWORD dwHotkey, char* pszOut, int cbOut )
{
    if( pszOut == NULL || cbOut <= 0 ) return;
    pszOut[0] = '\0';

    if( dwHotkey == 0 )
    {
        strncpy( pszOut, "（未设置）", cbOut - 1 );
        pszOut[cbOut - 1] = '\0';
        return;
    }

    BYTE bVk  = (BYTE)( dwHotkey & 0xFF );
    BYTE bMod = (BYTE)( (dwHotkey >> 8) & 0xFF );

    char szText[128];
    szText[0] = '\0';

    if( bMod & HOTKEYF_CONTROL_ ) strcat( szText, "Ctrl+" );
    if( bMod & HOTKEYF_ALT_ )     strcat( szText, "Alt+" );
    if( bMod & HOTKEYF_SHIFT_ )   strcat( szText, "Shift+" );
    if( bMod & HOTKEYF_EXT_ )     strcat( szText, "Win+" );

    char szKey[64];
    GetKeyName( bVk, szKey, sizeof(szKey) );
    strncat( szText, szKey, sizeof(szText) - strlen(szText) - 1 );

    strncpy( pszOut, szText, cbOut - 1 );
    pszOut[cbOut - 1] = '\0';
}

DWORD NekoHotkeyFromControl( WORD wHotkey )
{
    //The HOTKEY common control stores the virtual key in the low byte and the
    //HOTKEYF_* flags in the high byte: exactly our storage layout.
    return (DWORD)wHotkey;
}

BOOL NekoHotkeyConflict( DWORD dwPauseKey, DWORD dwHideKey )
{
    //Two disabled keys are not a conflict. A key equal to the other slot's value
    //(including 0 == 0) only counts when it is actually a real combination.
    if( dwPauseKey == 0 || dwHideKey == 0 ) return FALSE;
    return ( dwPauseKey == dwHideKey );
}

BOOL NekoHotkeyIsUsable( DWORD dwHotkey )
{
    if( dwHotkey == 0 ) return FALSE;

    BYTE bVk  = (BYTE)( dwHotkey & 0xFF );
    BYTE bMod = (BYTE)( (dwHotkey >> 8) & 0xFF );

    //A key is required; a modifier alone is not a combination.
    if( bVk == 0 ) return FALSE;

    //A bare key would steal that key from every other program on the desktop,
    //which is almost never what the user wants. Require Ctrl, Alt, Shift or Win.
    if( ( bMod & ( HOTKEYF_CONTROL_ | HOTKEYF_ALT_ | HOTKEYF_SHIFT_ | HOTKEYF_EXT_ ) ) == 0 )
        return FALSE;

    return TRUE;
}
