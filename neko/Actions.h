///////////////////////////////////////////////////////////////////////////////
// Actions.h
//
// The three actions a shortcut can perform besides hiding the cat and flipping
// the proxy. Kept together because they share one property that matters: each is
// a *switch* the user can turn on and off with the same key, and each reports a
// state so the tray tool tip and the settings list can show what is in force.
///////////////////////////////////////////////////////////////////////////////

#ifndef __ACTIONS_H__
#define __ACTIONS_H__

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

//---------------------------------------------------------------- keep awake
//Stops Windows from sleeping or blanking the screen on its own. The user asked
//for this as a toggle, so the state is mirrored in a global that the UI can read.
//
//Implementation note: SetThreadExecutionState is per THREAD. ES_CONTINUOUS with
//ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED holds the request open until it is
//cleared, so the call has to be made once from a thread that lives as long as
//the pet - the GUI thread - and the flag must never be set from a worker thread
//that dies, because the request dies with it.
//
//A process that exits drops its request automatically, which is the behaviour we
//want: a crashed pet must not keep the machine awake forever.
BOOL SleepGuardIsActive();
BOOL SleepGuardSetActive( BOOL bActive );
//Flip the request and return the state it is in afterwards (TRUE = sleep is
//being blocked), so the caller can update its display without a second query.
BOOL SleepGuardToggle();

//---------------------------------------------------------------- open a page
//Which browser to start. "custom" uses the command line the user typed, so an
//unusual browser works just as well as the two known ones.
#define NEKO_BROWSER_EDGE   0
#define NEKO_BROWSER_CHROME 1
#define NEKO_BROWSER_CUSTOM 2

//Browser identifiers as persisted in the list ("edge"/"chrome"/"custom").
const char* WebLauncherBrowserId( int nBrowser );
int         WebLauncherBrowserFromId( const char* szId );
const char* WebLauncherBrowserName( int nBrowser );

//Open a URL. szBrowserId is one of the persisted identifiers; szCustomPath is
//only used for "custom".
//
//The URL is passed as a separate command-line argument rather than glued into a
//shell string: a site address can contain '&' or '"', and letting cmd.exe see
//those would turn a shortcut into a command injection.
BOOL WebLauncherOpen( const char* szUrl, const char* szBrowserId, const char* szCustomPath );

//Resolve a browser to a full executable path. Returns FALSE when it is not
//installed. Exposed so the settings dialog can tell the user before they save a
//shortcut that would do nothing.
BOOL WebLauncherFindExecutable( int nBrowser, char* szOut, int cbOut );

//---------------------------------------------------------------- projection
//DisplaySwitch.exe lives in System32 and is the supported way to change how the
//desktop is spread across monitors; it is what Win+P drives.
//
//A shortcut of this kind holds TWO modes and flips between them, because that is
//what the user asked for ("在两套投影状态之间来回切换"). The entry remembers which
//one it applied last so the flip keeps working even after an unrelated display
//change.
#define NEKO_PROJ_MODE_A 0
#define NEKO_PROJ_MODE_B 1

//Apply one of the four modes by index (0 屏幕1, 1 屏幕2, 2 扩展, 3 辅助).
BOOL ProjectionApply( int nModeIndex );

//Flip the given shortcut between its two modes and return the index that was
//applied, or -1 on failure.
int ProjectionToggle( const char* szModeA, const char* szModeB );

#endif //__ACTIONS_H__
