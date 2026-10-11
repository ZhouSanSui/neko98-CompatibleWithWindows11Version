/************************************
 
   Tray Class: Very simple interface
   onto the system tray for Neko95

 *************************************/

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "tray.h"
#include "resource.h"

UINT g_uTaskbarCreated = 0;

//Fill in the parts of NOTIFYICONDATA that every call needs.
static void FillBase( NOTIFYICONDATA* pnid, HWND hWnd, UINT uID )
{
    memset( pnid, 0, sizeof(NOTIFYICONDATA) );
    pnid->cbSize = sizeof(NOTIFYICONDATA);
    pnid->hWnd = hWnd;
    pnid->uID = uID;
}

//Copy a tool tip into the fixed-size field, always NUL terminated. The field is
//NOT larger than 64 characters, but on older targets it was smaller, so never
//trust that the string fits.
static void CopyTip( char* pszDst, size_t cbDst, LPCSTR szTip )
{
    if( szTip == NULL ) szTip = "Neko";
    if( cbDst == 0 ) return;
    strncpy( pszDst, szTip, cbDst - 1 );
    pszDst[cbDst - 1] = '\0';
}

BOOL CTray::AddIcon( HWND hWnd, HICON hIcon, UINT uID, LPCSTR szTip )
{
    BOOL res; 
    NOTIFYICONDATA tnid; 

    //fill in the structure
    FillBase( &tnid, hWnd, uID );
    tnid.uFlags = NIF_MESSAGE|NIF_ICON|NIF_TIP; 
    tnid.uCallbackMessage = MY_NOTIFYICON; 
    tnid.hIcon = hIcon;
    CopyTip( tnid.szTip, sizeof(tnid.szTip), szTip );

    //give the command
    res = Shell_NotifyIcon( NIM_ADD, &tnid ); 
 
    //NOTE: the icon handle deliberately lives on. It used to be destroyed here,
    //but the caller normally passes the result of LoadIcon(), which is a handle
    //shared with the rest of the process (and possibly the cache). Destroying a
    //shared handle can make other windows draw a blank or corrupted icon, so the
    //ownership now sits with the caller.

    //if the shell had an icon already, make sure ours is really the one in place
    if( res )
    {
        //Ask for the modern notification behaviour (real tooltips, balloon
        //notifications routed to us) rather than the Windows 95 style.
        tnid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIcon( NIM_SETVERSION, &tnid );

        m_uIconCount++;
    }

    return res; 
}

BOOL CTray::RemoveIcon( HWND hWnd, UINT uID )
{
    BOOL res;
    NOTIFYICONDATA tnid;

    //fill in the structure
    FillBase( &tnid, hWnd, uID );

    //give the notify command
    res = Shell_NotifyIcon( NIM_DELETE, &tnid );

    //decrement the counter
    if( res ) m_uIconCount--;

    return res;
}

BOOL CTray::SetTip( HWND hWnd, UINT uID, LPCSTR szTip )
{
    NOTIFYICONDATA tnid;

    FillBase( &tnid, hWnd, uID );
    tnid.uFlags = NIF_TIP;
    CopyTip( tnid.szTip, sizeof(tnid.szTip), szTip );

    return Shell_NotifyIcon( NIM_MODIFY, &tnid );
}

BOOL CTray::SetIcon( HWND hWnd, HICON hIcon, UINT uID )
{
    NOTIFYICONDATA tnid;

    FillBase( &tnid, hWnd, uID );
    tnid.uFlags = NIF_ICON;
    tnid.hIcon = hIcon;

    return Shell_NotifyIcon( NIM_MODIFY, &tnid );
}

/* Build a menu label, optionally with the hot key appended in brackets so the
   user can see the shortcut without opening the settings dialog. */
static void MakeLabel( char* pszDst, size_t cbDst, LPCSTR szText, LPCSTR szHint )
{
    if( cbDst == 0 ) return;

    if( szHint == NULL || szHint[0] == '\0' )
    {
        strncpy( pszDst, szText, cbDst - 1 );
        pszDst[cbDst - 1] = '\0';
        return;
    }

    //"\t" is the conventional accelerator column and makes the menu line up.
    _snprintf( pszDst, cbDst, "%s\t%s", szText, szHint );
    pszDst[cbDst - 1] = '\0';
}

void CTray::ShowPopupMenu( HWND hWnd, BOOL bPaused, BOOL bHidden,
                           LPCSTR szPauseHint, LPCSTR szHideHint )
{
    //get the mouse position
    POINT mouse;
    GetCursorPos( &mouse );

    //create the popup menu
    HMENU hMenu = CreatePopupMenu();
    if( hMenu == NULL ) return;

    char szPause[96], szHide[96];

    MakeLabel( szPause, sizeof(szPause), bPaused ? "继续(&C)" : "暂停(&P)", szPauseHint );
    MakeLabel( szHide,  sizeof(szHide),  bHidden ? "显示小猫(&W)" : "隐藏小猫(&W)", szHideHint );

    AppendMenu( hMenu, MF_STRING, ID_SETTINGS, "设置(&S)..." );
    AppendMenu( hMenu, MF_SEPARATOR, 0, NULL );
    AppendMenu( hMenu, MF_STRING, ID_TRAY_PAUSE, szPause );
    AppendMenu( hMenu, MF_STRING, ID_TRAY_HIDE,  szHide );
    AppendMenu( hMenu, MF_SEPARATOR, 0, NULL );
    AppendMenu( hMenu, MF_STRING, ID_EXIT,    "退出(&E)" );
    SetMenuDefaultItem( hMenu, ID_SETTINGS, FALSE );

    //display the menu
    TrackPopupMenu( hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWnd, NULL );
 
    //destroy it
    DestroyMenu(hMenu); 
}
