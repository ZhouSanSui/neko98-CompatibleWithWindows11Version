/************************************
 
   Tray Class: Very simple interface
   onto the system tray

 *************************************/

#ifndef _TRAY_HPP
#define _TRAY_HPP

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

//taskbar notification message
#define MY_NOTIFYICON (WM_USER+32)

//The shell broadcasts this registered message after the task bar is (re)created,
//which happens on every Explorer restart. Without listening for it the icon would
//be lost until the pet is restarted. The message id is resolved at run time.
extern UINT g_uTaskbarCreated;


class CTray {

public:
    CTray( HINSTANCE hInstance ) { m_uIconCount = 0; m_hInstance = hInstance; };

    //AddIcon no longer destroys the handle it is given: the icon passed in may be
    //a shared handle owned by the system (LoadIcon), and destroying it corrupts a
    //resource that other windows share. The caller owns the lifetime.
    BOOL AddIcon( HWND hWnd, HICON hIcon, UINT uID, LPCSTR szTip = "Neko" );
    BOOL RemoveIcon( HWND hWnd, UINT uID );

    //Change the tool tip or the picture without removing the icon. Both return
    //FALSE when the icon is not currently present.
    BOOL SetTip( HWND hWnd, UINT uID, LPCSTR szTip );
    BOOL SetIcon( HWND hWnd, HICON hIcon, UINT uID );

    //The menu now also offers pause/hide, so it needs to know the current state.
    //The two hint strings, when given, are the user's own hot keys rendered as
    //text (e.g. "Ctrl+Alt+N") and are appended to the item labels so the
    //shortcuts are discoverable without opening the settings dialog.
    void ShowPopupMenu( HWND hWnd, BOOL bPaused, BOOL bHidden,
                        LPCSTR szPauseHint = NULL, LPCSTR szHideHint = NULL );

    UINT GetCount() { return m_uIconCount; };

private:
    UINT m_uIconCount;
    HINSTANCE m_hInstance;
};

#endif
