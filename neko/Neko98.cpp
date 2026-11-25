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
#include "HotkeyList.h"
#include "Actions.h"
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

//Which shortcut entries are actually registered with the system, and whether the
//user was already warned that one could not be (so we complain once rather than
//on every reload).
//
//Ids are NEKO_HOTKEY_ID_BASE + index into g_HotkeyList. A fixed base keeps them
//clear of any id the rest of the program might use, and the index is what lets
//WM_HOTKEY find the entry whose action should run.
#define NEKO_HOTKEY_ID_BASE 100
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

//Forward: LoadConfiguration re-applies the shortcut list, and the list registration
//needs the same handler table defined further down.
void ApplyHotkeyList( BOOL bReportErrors );

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

    //Reload the shortcut list and re-register every combination. The settings
    //dialog only tells us "reload", so this is where a newly added, edited or
    //removed shortcut actually takes effect.
    ApplyHotkeyList( TRUE );
}


/* ApplyHotkeyList - (re)register every shortcut in the user's list ****************/
//
//Called on start-up and after the settings dialog saves. Registrations are keyed
//by list INDEX, so an entry that moved keeps working and one that was deleted is
//released. A combination that cannot be registered (another program owns it, or
//two entries were hand-edited to the same key) is reported once: silently doing
//nothing is the failure mode the user cannot diagnose.
void ApplyHotkeyList( BOOL bReportErrors )
{
    int i;

    //Drop everything we held before, so a deleted or edited entry cannot leave a
    //stale registration behind.
    NekoHotkeyUnregisterAll( g_hWndMain );

    HotkeyListLoad();

    int nFailed = 0;
    char szFirstBad[128];
    szFirstBad[0] = '\0';

    for( i = 0; i < g_nHotkeyList; i++ )
    {
        if( g_HotkeyList[i].dwHotkey == 0 ) continue;   //deliberately unbound

        if( !NekoHotkeyRegister( g_hWndMain, NEKO_HOTKEY_ID_BASE + i,
                                 g_HotkeyList[i].dwHotkey ) )
        {
            nFailed++;
            if( szFirstBad[0] == '\0' )
            {
                const char* szName = HotkeyTypeName( g_HotkeyList[i].dwType );
                strncpy( szFirstBad, szName, sizeof szFirstBad - 1 );
                szFirstBad[sizeof szFirstBad - 1] = '\0';
            }
        }
    }

    if( nFailed > 0 && bReportErrors && !g_fHotkeyFailed )
    {
        g_fHotkeyFailed = TRUE;
        char szMsg[512];
        _snprintf( szMsg, sizeof szMsg,
                   "有 %d 个全局快捷键无法注册（例如“%s”）。\n"
                   "可能已被其他程序占用，或与另一个快捷键重复。\n请在设置中换一个组合。",
                   nFailed, szFirstBad );
        szMsg[sizeof szMsg - 1] = '\0';
        MessageBox( g_hWndMain, szMsg, "Neko 快捷键", MB_ICONINFORMATION );
    }
    else if( nFailed == 0 )
    {
        //Everything is in force again: allow a future warning to be shown.
        g_fHotkeyFailed = FALSE;
    }
}


/* HandleProxyHotkey - switch the Windows manual proxy ****************************/
//
//The address comes from the shortcut entry that was pressed, so two proxy
//shortcuts can point at two different servers. The state is reported back through
//the tray tool tip so the user gets feedback without opening Settings.
static void HandleProxyHotkey( const char* szServer )
{
    BOOL bWas = ProxyIsEnabled();
    BOOL bNow = ProxyToggleEx( szServer );

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


/* RunHotkeyAction - dispatch one entry of the shortcut list *********************/
//
//One function so the hot key, the tray menu and the settings preview all drive
//exactly the same code path; anything else is how a menu item and a key quietly
//start behaving differently.
static void RunHotkeyAction( const NEKOHOTKEY* lpEntry )
{
    if( lpEntry == NULL ) return;

    switch( lpEntry->dwType )
    {
        case NEKO_HT_HIDE:
            //The three-state cycle: normal -> magnified -> hidden -> normal.
            HandleHideHotkey();
            break;

        case NEKO_HT_PROXY:
        {
            //The entry stores the two parts separately so the settings dialog can
            //edit them as two boxes; ProxyToggle wants one "host:port" string.
            char szServer[NEKO_HT_P1 + NEKO_HT_P2 + 2];
            _snprintf( szServer, sizeof szServer, "%s:%s", lpEntry->szP1, lpEntry->szP2 );
            szServer[sizeof szServer - 1] = '\0';
            HandleProxyHotkey( szServer );
            break;
        }

        case NEKO_HT_NOSLEEP:
            SleepGuardToggle();
            ApplyTrayState();
            break;

        case NEKO_HT_WEBSITE:
            if( !WebLauncherOpen( lpEntry->szP1, lpEntry->szP2, lpEntry->szP2 ) )
            {
                MessageBox( NULL,
                            "无法打开指定的浏览器。\n请检查“打开网站”快捷方式里选择的浏览器是否正确。",
                            "Neko98", MB_ICONWARNING | MB_TASKMODAL );
            }
            break;

        case NEKO_HT_PROJECTION:
            if( ProjectionToggle( lpEntry->szP1, lpEntry->szP2 ) < 0 )
            {
                MessageBox( NULL,
                            "无法切换投影方式。\n请检查快捷方式里选择的两个显示模式是否不同。",
                            "Neko98", MB_ICONWARNING | MB_TASKMODAL );
            }
            break;
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
                    //Same entry the shortcut list uses, so the menu and the key
                    //drive exactly the same code path (with the same address).
                    {
                        NEKOHOTKEY entry;
                        memset( &entry, 0, sizeof entry );
                        entry.dwType = NEKO_HT_PROXY;
                        //Use whatever address is configured in the first proxy
                        //shortcut, so the menu agrees with the key.
                        int i;
                        BOOL bFound = FALSE;
                        for( i = 0; i < g_nHotkeyList; i++ )
                        {
                            if( g_HotkeyList[i].dwType == NEKO_HT_PROXY )
                            {
                                entry = g_HotkeyList[i];
                                bFound = TRUE;
                                break;
                            }
                        }
                        if( bFound ) RunHotkeyAction( &entry );
                        else
                        {
                            //No proxy entry configured: fall back to the built-in
                            //address so the menu item still does something.
                            HandleProxyHotkey( NULL );
                        }
                    }
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
            //Ids are NEKO_HOTKEY_ID_BASE + the entry's index in the list, so this
            //is where a shortcut finds out WHICH action the user bound it to.
            //Anything outside the current list is stale (the list shrank since
            //the registration) and is simply ignored.
            {
                int nIndex = (int)wParam - NEKO_HOTKEY_ID_BASE;
                if( nIndex >= 0 && nIndex < g_nHotkeyList )
                    RunHotkeyAction( &g_HotkeyList[nIndex] );
            }
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
                        //The hints come from the shortcut LIST now, so the menu
                        //shows whatever combination is really bound to the
                        //hide/show and proxy actions (or nothing when the user
                        //removed that entry).
                        char szPauseHint[64], szHideHint[64], szProxyHint[64];
                        szPauseHint[0] = '\0';
                        szHideHint[0]  = '\0';
                        szProxyHint[0] = '\0';

                        for( int i = 0; i < g_nHotkeyList; i++ )
                        {
                            if( g_HotkeyList[i].dwType == NEKO_HT_HIDE && szHideHint[0] == '\0' )
                                NekoHotkeyFormat( g_HotkeyList[i].dwHotkey, szHideHint, sizeof(szHideHint) );
                            else if( g_HotkeyList[i].dwType == NEKO_HT_PROXY && szProxyHint[0] == '\0' )
                                NekoHotkeyFormat( g_HotkeyList[i].dwHotkey, szProxyHint, sizeof(szProxyHint) );
                        }

                        //An unbound entry would render as "（未设置）"; suppress
                        //that in the menu instead of showing a meaningless hint.
                        if( strcmp( szHideHint, "（未设置）" ) == 0 )  szHideHint[0]  = '\0';
                        if( strcmp( szProxyHint, "（未设置）" ) == 0 ) szProxyHint[0] = '\0';

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

    //Register the user's global shortcuts. The list is created with the two
    //defaults the previous build shipped the first time this build runs, and is
    //migrated from the old two fixed slots so an upgrading user keeps their keys.
    ApplyHotkeyList( FALSE );

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
    NekoHotkeyUnregisterAll( g_hWndMain );
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
