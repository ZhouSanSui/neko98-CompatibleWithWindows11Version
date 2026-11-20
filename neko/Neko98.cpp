/************************************
	Neko for Windows
 *************************************/

#include <windows.h> 
#include <commctrl.h>
#include <wtsapi32.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "Tray.h"
#include "NekoSettings.h"
#include "AutoStart.h"
#include "Hotkey.h"
#include "ProxyToggle.h"
#include "NekoCommon.h"
#include "resource.h"
#include "Neko.h"

//global variables
HINSTANCE g_hInstance = NULL;
HWND g_hWndMain = NULL;
BOOL g_fTaskbar = TRUE;
CTray * Tray = NULL;

int g_nNumCats = 0;
typedef CNeko* PNeko;
PNeko* g_NekoArray = NULL; //array of CNeko pointers

//Interval, in milliseconds, between pet updates. Derived from the number of
//cats so a busy desktop stays responsive.
DWORD g_dwSleepTime = 200;

//Paused: the cats keep their position but stop moving. Hidden: the cats are not
//drawn at all. Magnified: drawn at 300% so the user can find them. All three are
//user choices; paused/hidden survive a restart, magnified is deliberately NOT
//restored, because coming back from a restart at triple size would be startling.
BOOL g_fPaused = FALSE;
BOOL g_fHidden = FALSE;
BOOL g_fMagnified = FALSE;

//How much the "make it big" state multiplies the drawn size by. The user asked
//for three times the normal size.
#define NEKO_MAGNIFY_FACTOR 3.0f

BOOL g_fNeedReinit = TRUE;

//The desktop geometry changed but the configuration did not (resolution change,
//monitor hot-plug, session lock/unlock). That only needs the pets to re-derive
//their surface, NOT a full reload that deletes and reconstructs every cat,
//re-extracts all 40 icons and teleports the pet to a new random position.
BOOL g_fNeedGeometryRefresh = FALSE;

//The hot keys currently in force, and whether the user was already warned that
//one could not be registered (so we complain once, not on every reload).
//Index 0 = pause/resume, index 1 = hide/show.
static DWORD g_dwHotkey = 0;
static DWORD g_dwHideHotkey = 0;
static BOOL  g_fHotkeyFailed = FALSE;

//Posted by a second copy of the program when the user launches it while a pet is
//already running: the running instance reveals itself instead of being ignored.
#define MY_SHOWNEKO (WM_USER+34)

//The task bar was rebuilt (Explorer restarted) - our icon has to be re-added.
extern UINT g_uTaskbarCreated;


/* Helper: does this window belong to our own process? ******************************/
static BOOL IsOwnWindow( HWND hWnd )
{
    DWORD dwPid = 0;
    if( hWnd == NULL ) return TRUE;
    GetWindowThreadProcessId( hWnd, &dwPid );
    return ( dwPid == GetCurrentProcessId() );
}

/* ExecuteConfigProcess - runs the configuration program *******************************/
void ExecuteConfigProcess()
{
    //Resolve the settings program next to this executable rather than relative to
    //the current directory: a shortcut can have any "start in" folder.
    char szPath[MAX_PATH];
    DWORD dw = GetModuleFileName( NULL, szPath, MAX_PATH );
    if( dw == 0 || dw >= MAX_PATH ) szPath[0] = '\0';

    char* pszSlash = strrchr( szPath, '\\' );
    if( pszSlash != NULL )
        strcpy( pszSlash + 1, "NekoCFG.EXE" );
    else
        strcpy( szPath, "NekoCFG.EXE" );

    if( (INT_PTR)ShellExecute( NULL, "open", szPath, 0, "", SW_SHOWNORMAL ) <= 32 )
        MessageBox( NULL, "Neko 无法启动配置程序\n'NekoCFG.EXE'\n\n请确认该文件与主程序位于同一文件夹。", "配置 Neko", MB_ICONEXCLAMATION );
}

/* ApplyTrayState - keep the tray icon and tool tip in step with our state *******/
static void ApplyTrayState()
{
    if( Tray == NULL ) return;

    if( g_fTaskbar )
    {
        const char* szTip = "Neko98";
        if( g_fPaused && g_fHidden )   szTip = "Neko98 - 已暂停（已隐藏）";
        else if( g_fPaused )           szTip = "Neko98 - 已暂停";
        else if( g_fHidden )           szTip = "Neko98 - 已隐藏";
        else if( g_fMagnified )        szTip = "Neko98 - 已放大 300%";

        if( Tray->GetCount() == 0 )
        {
            //LoadIcon hands out a shared handle, so it is never destroyed here.
            //CTray::AddIcon no longer destroys the handle it is given.
            HICON hIcon = LoadIcon( g_hInstance, MAKEINTRESOURCE( IDI_AWAKE ) );
            Tray->AddIcon( g_hWndMain, hIcon, 1, szTip );
        }
        else
        {
            Tray->SetTip( g_hWndMain, 1, szTip );
        }
    }
    else
    {
        if( Tray->GetCount() > 0 ) Tray->RemoveIcon( g_hWndMain, 1 );
    }
}

/* ApplyFootprintHint - one side of the trail turns white while the proxy is on ***/
//Reads the LIVE proxy state rather than remembering it, because the user can also
//change it from Windows Settings or another program, and a stale hint would then
//claim the proxy is on when it is not.
static void ApplyFootprintHint()
{
    BOOL bOn = ProxyIsEnabled();

    for( int i = 0; i < g_nNumCats; i++ )
        if( g_NekoArray[i] != NULL ) g_NekoArray[i]->SetFootprintHighlight( bOn );
}

/* ApplyPetState - show, hide or pause every cat and persist the choice *********/
static void ApplyPetState( BOOL bPersist )
{
    int i;
    float fMagnify = g_fMagnified ? NEKO_MAGNIFY_FACTOR : 1.0f;

    for( i = 0; i < g_nNumCats; i++ )
    {
        if( g_NekoArray[i] == NULL ) continue;

        //Size first, then visibility: resizing a hidden window is harmless, but
        //showing one that is still at the old size would flash the wrong size
        //for a frame.
        g_NekoArray[i]->SetMagnify( fMagnify );
        g_NekoArray[i]->SetVisible( g_fHidden ? FALSE : TRUE );
    }

    if( bPersist )
    {
        CNekoSettings settings( szNekoRegKey, TRUE );
        if( settings.IsOpen() )
        {
            settings.SetBool( szNekoPausedKey, g_fPaused );
            settings.SetBool( szNekoHiddenKey, g_fHidden );
        }
    }

    ApplyTrayState();
    ApplyFootprintHint();
}

/* LoadConfiguration - reloads the program configuration *******************************/
void LoadConfiguration()
{
    //load in the new global settings
    CNekoSettings * NekoSettings = new CNekoSettings( szNekoRegKey );
    NekoSettings->GetBool( szNekoTaskbarKey, &g_fTaskbar );
    NekoSettings->GetBool( szNekoPausedKey, &g_fPaused );
    NekoSettings->GetBool( szNekoHiddenKey, &g_fHidden );

    //delete all existing Nekos
    int i;
    for( i = 0; i < g_nNumCats; i++ ) delete g_NekoArray[i];
    delete[] g_NekoArray;
    g_NekoArray = NULL;
    g_nNumCats = 0;

    //load in new cats list
    DWORD dwNumCats = 0;
    NekoSettings->GetInt( szNekoNumCatsKey, &dwNumCats );

    //A hand-edited or corrupt registry must not be able to create thousands of
    //cats (or a negative one): clamp to something sane.
    if( dwNumCats > MAX_NEKO_CATS ) dwNumCats = MAX_NEKO_CATS;
    g_nNumCats = (int)dwNumCats;

    if( g_nNumCats == 0 )
    {
        //the user hasn't run the config program, or there are no Nekos - use default
        g_NekoArray = new PNeko[1];
        g_NekoArray[0] = new CNeko();
        g_nNumCats = 1;
    }
    else
    {
        g_NekoArray = new PNeko[g_nNumCats];
        for( i = 0; i < g_nNumCats; i++ )
        {
            //build the key index
            char szSubKey[MAX_PATH], szName[MAX_NEKO_NAME];
            szName[0] = '\0';
            sprintf( szSubKey, "%d", i );

            //load this cat's name from the registry
		    NekoSettings->GetString( szSubKey, szName, MAX_NEKO_NAME );
            szName[MAX_NEKO_NAME - 1] = '\0';

            //add this Neko to the array
			g_NekoArray[i] = new CNeko( szName );
        }
    }

	//erase the Neko settings
    delete NekoSettings;

	//calculate new sleep time: more cats means a slightly shorter interval so the
	//whole desktop keeps moving smoothly, but never faster than 100 ms.
	g_dwSleepTime = 200;
	if( g_nNumCats > 0 && g_nNumCats <= MAX_NEKO_CATS )
	{
		DWORD dwCalc = 200 - ( 5 * (DWORD)g_nNumCats );
		g_dwSleepTime = ( dwCalc < 100 ) ? 100 : dwCalc;
	}

	//(re)apply the current interval
	if( g_hWndMain != NULL )
	{
		KillTimer( g_hWndMain, ID_TIMER_NEKO );
		SetTimer( g_hWndMain, ID_TIMER_NEKO, g_dwSleepTime, NULL );
	}

	//make the new cats match the pause/hide state
	ApplyPetState( FALSE );

    //update task bar
    ApplyTrayState();

    //The user may have chosen new combinations in the settings dialog, and the
    //settings dialog only tells us "reload". Re-apply each hot key when it moved.
    BOOL fHotkeyChanged = FALSE;

    DWORD dwWant = NekoHotkeyLoad( FALSE );
    if( dwWant != g_dwHotkey || NekoHotkeyCurrent( FALSE ) != dwWant )
    {
        g_dwHotkey = dwWant;
        fHotkeyChanged = TRUE;
        if( dwWant != 0 && !NekoHotkeyApply( g_hWndMain, dwWant, FALSE ) )
        {
            //Another program already owns the combination, or both slots were
            //given the same combination. Say so once, rather than silently doing
            //nothing every time settings are applied.
            if( !g_fHotkeyFailed )
            {
                g_fHotkeyFailed = TRUE;
                MessageBox( g_hWndMain,
                            "所设置的全局快捷键已被其他程序占用（或与另一个快捷键重复），Neko 无法使用它。\n请在设置中换一个组合。",
                            "Neko 快捷键", MB_ICONINFORMATION );
            }
        }
    }

    DWORD dwWantHide = NekoHotkeyLoad( TRUE );
    if( dwWantHide != g_dwHideHotkey || NekoHotkeyCurrent( TRUE ) != dwWantHide )
    {
        g_dwHideHotkey = dwWantHide;
        fHotkeyChanged = TRUE;
        if( dwWantHide != 0 && !NekoHotkeyApply( g_hWndMain, dwWantHide, TRUE ) )
        {
            if( !g_fHotkeyFailed )
            {
                g_fHotkeyFailed = TRUE;
                MessageBox( g_hWndMain,
                            "所设置的“隐藏/显示”快捷键已被其他程序占用（或与另一个快捷键重复），Neko 无法使用它。\n请在设置中换一个组合。",
                            "Neko 快捷键", MB_ICONINFORMATION );
            }
        }
    }

    //Clear the one-time warning as soon as both combinations are in force again.
    if( fHotkeyChanged && NekoHotkeyCurrent( FALSE ) == g_dwHotkey &&
        NekoHotkeyCurrent( TRUE ) == g_dwHideHotkey )
    {
        g_fHotkeyFailed = FALSE;
    }
}


/* HandleProxyHotkey - the first hot key now switches the Windows manual proxy ***/
//
//This key used to pause the cats. The user asked for it to control the system
//proxy instead, so pausing moved to the tray menu (where it still lives) and the
//key now flips the manual proxy on and off. The state is reported back through
//the tray tool tip so the user gets feedback without opening Settings.
static void HandleProxyHotkey()
{
    BOOL bWas = ProxyIsEnabled();
    BOOL bNow = ProxyToggle();

    //Silent when the switch really moved; only complain when the registry write
    //was refused, because otherwise a failing hot key is invisible.
    if( bNow == bWas )
    {
        MessageBox( NULL,
                    "无法切换系统代理。\n请确认当前用户有权修改 Internet 设置。",
                    "Neko98", MB_ICONWARNING | MB_TASKMODAL );
    }

    //Refresh the tool tip so the new proxy state is visible at a glance.
    ApplyTrayState();

    //Footprints are the second, in-world cue: with the proxy on, one side of the
    //trail turns white. Done here (and not only in ApplyPetState) so the change
    //takes effect on the very next step instead of waiting for another state
    //change to trigger a refresh.
    ApplyFootprintHint();
}


/* HandleHideHotkey - three-state cycle: normal -> magnified -> hidden -> normal ***/
//
//The user asked for one key that walks a cycle instead of a plain toggle:
//   1st press  the cat grows to 300% (still on screen)
//   2nd press  the cat disappears
//   3rd press  the cat comes back at normal size, and MUST really be back
//
//The magnified step exists so the key is useful even when the cat is somewhere
//behind a window: the first press makes it big enough to notice, and only the
//second one hides it. The final step is deliberately the forceful one, because
//the failure being guarded against is "the process is running but no cat is on
//the desktop" - so coming back must not merely clear a flag, it must re-anchor
//the window, re-assert topmost and re-composite.
static void ForceShowPets();     //defined just below

static void HandleHideHotkey()
{
    if( g_fHidden )
    {
        //---- hidden -> shown (the mandatory restore) -----------------------
        g_fHidden = FALSE;
        g_fPaused = FALSE;
        g_fMagnified = FALSE;

        ApplyPetState( TRUE );
        ForceShowPets();
        return;
    }

    if( g_fMagnified )
    {
        //---- magnified -> hidden ------------------------------------------
        //Leave the magnification flag alone so that coming back through the
        //first branch restores normal size rather than re-entering the cycle
        //half way through.
        g_fHidden = TRUE;

        ApplyPetState( TRUE );
        return;
    }

    //---- normal -> magnified ---------------------------------------------
    g_fMagnified = TRUE;
    g_fPaused = FALSE;

    ApplyPetState( TRUE );
}

/* ForceShowPets - the "really bring it back" path ********************************/
static void ForceShowPets()
{
    int i;
    for( i = 0; i < g_nNumCats; i++ )
    {
        if( g_NekoArray[i] == NULL ) continue;
        g_NekoArray[i]->ForceShow();
    }
}


/* WndProc - message processing function for the hidden Neko window ********************/
LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    //Explorer restarted: put our icon back. This has to be checked before the
    //switch because the id is only known at run time.
    if( g_uTaskbarCreated != 0 && message == g_uTaskbarCreated )
    {
        if( Tray != NULL && Tray->GetCount() > 0 )
        {
            //The shell forgot us, so reset our own count first.
            Tray->RemoveIcon( hWnd, 1 );
        }
        ApplyTrayState();
        return 0;
    }

    switch( message )
    {
        case WM_DESTROY:
			//terminate the program
            PostQuitMessage( 0 );
            break;

        case WM_COMMAND:
            switch(LOWORD(wParam))
            {
                case ID_SETTINGS:
                    ExecuteConfigProcess();
                    break;

                case ID_TRAY_PROXY:
                    //Same helper as the hot key, so the menu and the key drive
                    //exactly the same code path.
                    HandleProxyHotkey();
                    break;

                case ID_TRAY_PAUSE:
                    g_fPaused = g_fPaused ? FALSE : TRUE;
                    ApplyPetState( TRUE );
                    break;

                case ID_TRAY_HIDE:
                    //Route the tray item through the SAME state machine as the
                    //hot key so the two can never drift apart: menu and key both
                    //walk normal -> magnified -> hidden -> normal.
                    HandleHideHotkey();
                    break;

                case ID_TRAY_MAGNIFY:
                    g_fMagnified = g_fMagnified ? FALSE : TRUE;
                    if( g_fMagnified ) g_fPaused = FALSE;
                    ApplyPetState( TRUE );
                    break;

                case ID_EXIT: 
                    PostQuitMessage( 0 ); 
                    break;
            }
            break;

        case WM_HOTKEY:
            //The first combination used to pause the cats; it now switches the
            //Windows manual proxy, as asked. Pausing stayed available in the tray
            //menu. The second combination still runs the hide/show cycle.
            if( wParam == NEKO_HOTKEY_ID )           HandleProxyHotkey();
            else if( wParam == NEKO_HIDE_HOTKEY_ID ) HandleHideHotkey();
            break;

        case MY_SHOWNEKO:
            //A second copy of the program was launched: make sure the cat is
            //actually visible again instead of leaving the user with nothing.
            g_fHidden = FALSE;
            g_fPaused = FALSE;
            g_fMagnified = FALSE;
            ApplyPetState( TRUE );
            ForceShowPets();
            break;

        //A display-mode change (resolution, monitor hot-plug) alters the desktop
        //geometry but not the user's configuration, so only refresh the pets'
        //geometry instead of rebuilding every cat from the registry.
        case WM_DEVMODECHANGE:
        case WM_DISPLAYCHANGE:
            g_fNeedGeometryRefresh = TRUE;
            break;

        //A settings change and an explicit request from the configuration
        //program DO need the full reload (the user may have changed anything).
        case WM_SETTINGCHANGE:
        case MY_UPDATENEKO:
            g_fNeedReinit = TRUE;
            break;

        //The workstation was locked/unlocked or a remote session connected or
        //disconnected. The desktop composition changes underneath us, so make
        //sure the next tick recomposites from scratch.
        case WM_WTSSESSION_CHANGE:
        case WM_POWERBROADCAST:
            g_fNeedGeometryRefresh = TRUE;
            break;

        case MY_NOTIFYICON:
            switch( lParam )
            {
                case WM_LBUTTONDBLCLK:
                    ExecuteConfigProcess();
                    break;

                case WM_RBUTTONUP:
                    //this line is needed to make the menu go away!
                    //(from Windows Developer FAQ [Robert Mashlan])
                    SetForegroundWindow( hWnd );

                    //show the context menu, with the user's own combinations
                    //displayed next to the items they trigger
                    if( Tray != NULL )
                    {
                        char szPauseHint[64], szHideHint[64], szProxyHint[64];
                        NekoHotkeyFormat( g_dwHotkey,     szProxyHint, sizeof(szProxyHint) );
                        NekoHotkeyFormat( g_dwHideHotkey, szHideHint,  sizeof(szHideHint) );
                        //the pause action has no hot key any more - it moved to the
                        //menu when the first combination became the proxy switch
                        szPauseHint[0] = '\0';
                        Tray->ShowPopupMenu( hWnd, g_fPaused, g_fHidden, g_fMagnified,
                                             ProxyIsEnabled(),
                                             szPauseHint, szHideHint, szProxyHint );
                    }
                    break;
            }
            break;

        case WM_TIMER:
        {
            //Only our own timer drives the cats.
            if( wParam != ID_TIMER_NEKO ) break;

            //check to see if stuff needs reinitialisation
            if( g_fNeedReinit )
            {
                LoadConfiguration();
                g_fNeedReinit = FALSE;
                g_fNeedGeometryRefresh = FALSE;
            }
            else if( g_fNeedGeometryRefresh )
            {
                //geometry-only refresh: keep the cats, their icons and their
                //positions; just re-derive surfaces for the new desktop.
                g_fNeedGeometryRefresh = FALSE;
                for( int i = 0; i < g_nNumCats; i++ )
                    if( g_NekoArray[i] != NULL ) g_NekoArray[i]->OnDisplayChange();
            }

			//A hidden or paused pet does not need to simulate at all, which also
			//saves the recompositing work.
			if( !g_fPaused && !g_fHidden )
			{
				//update all Nekos
				for( int i = 0; i < g_nNumCats; i++ )
                    if( g_NekoArray[i] != NULL ) g_NekoArray[i]->Update();
			}
            break;
        }

        default: 
            return DefWindowProc( hWnd, message, wParam, lParam );
    }

    return 0;
}


/* InitApplication - program initialisation function ***********************************/
BOOL InitApplication( HINSTANCE hInstance )
{
    //store the instance handle
    g_hInstance = hInstance;

	//create the (hidden) window
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = szNekoClassName;

    if( RegisterClass(&wc) )
    {
        //create the window
        g_hWndMain = CreateWindow( szNekoClassName, szNekoWindowTitle, WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

		if( g_hWndMain == NULL ) return FALSE;
    }
	else
		return FALSE;

    //Find out the id of the "task bar recreated" broadcast so the tray icon can
    //come back after Explorer restarts.
    g_uTaskbarCreated = RegisterWindowMessage( "TaskbarCreated" );

    //Listen for lock/unlock and remote-session changes.
    WTSRegisterSessionNotification( g_hWndMain, NOTIFY_FOR_THIS_SESSION );

    //initialise random number generator
    srand( GetTickCount() );

    //If the user enabled auto-start, keep the stored path in step with where the
    //program actually lives now, so moving the folder does not silently break
    //start-up. Does nothing when auto-start was never enabled.
    NekoAutoStartRepair();

    return TRUE;
}


/* WinMain - program entry point *******************************************************/
int CALLBACK WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
	//check for previous version & unload both if it's found
	HWND hWndOld = FindWindow( szNekoClassName, szNekoWindowTitle );
	if( hWndOld != NULL )
	{
		//Ask the running copy to reveal itself, then step aside. (The old code
		//posted WM_QUIT, which a window procedure does not act on, so the result
		//was simply "the second copy exits" - the user got no feedback at all.)
		PostMessage( hWndOld, MY_SHOWNEKO, 0, 0 );
		return FALSE;
	}

    //initialise the program
    if( !InitApplication( hInstance ) ) return FALSE;

    //create the tray object
    Tray = new CTray( hInstance );

    //Register the user's global hot keys. On the very first run there is no
    //setting stored, in which case the default combinations are used. The
    //pause/resume key is applied first so it wins if both slots were (perhaps by
    //hand-edited registry) set to the same combination.
    g_dwHotkey     = NekoHotkeyLoad( FALSE );
    g_dwHideHotkey = NekoHotkeyLoad( TRUE );

    if( g_dwHotkey != 0 )
    {
        if( !NekoHotkeyApply( g_hWndMain, g_dwHotkey, FALSE ) )
            g_fHotkeyFailed = TRUE;
    }
    if( g_dwHideHotkey != 0 )
    {
        if( !NekoHotkeyApply( g_hWndMain, g_dwHideHotkey, TRUE ) )
            g_fHotkeyFailed = TRUE;
    }

    //set a timer to update Neko (the interval is set once the configuration has
    //been read, and LoadConfiguration() re-applies it from then on)
    SetTimer( g_hWndMain, ID_TIMER_NEKO, 200, NULL );

    MSG msg;
    while( GetMessage( &msg, NULL, 0, 0 ) )
    {
		//pass the message onto our WndProc
		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}

    //remove the timer
    KillTimer( g_hWndMain, ID_TIMER_NEKO );

    //release the hot keys and stop listening for session changes
    NekoHotkeyReleaseAll( g_hWndMain );
    WTSUnRegisterSessionNotification( g_hWndMain );

    //delete all Nekos
    for( int i = 0; i < g_nNumCats; i++ ) delete g_NekoArray[i];
    delete[] g_NekoArray;

    //remove the taskbar icon
    if( g_fTaskbar ) Tray->RemoveIcon( g_hWndMain, 1 );
    delete Tray;

    return msg.wParam;
}
