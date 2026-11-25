///////////////////////////////////////////////////////////////////////////////
// HotkeyList.h
//
// The user-configurable list of global shortcuts.
//
// Windows registers a global hot key by its key combination, so a program that
// wants several independent shortcuts has to own several registrations and tell
// them apart by the id it passed to RegisterHotKey. This module owns the list
// and its storage; Neko98.cpp owns the registration and dispatch.
//
// Why a list and not two fixed slots
// ----------------------------------
// The previous build hard-coded exactly two combinations (one for the proxy, one
// for the hide/magnify cycle). The user asked for five *kinds* of action, and for
// the freedom to add more than one shortcut of the same kind (two different web
// sites, two different proxies). A fixed pair of slots cannot express that, so
// the settings are now an ordered list of entries, each carrying its own
// combination and its own parameters.
//
// Storage
// -------
// One REG_SZ value under Neko's own key, named "HotkeyList". Each entry is one
// line; the fields inside a line are separated by a TAB:
//
//     type <TAB> hotkey <TAB> param1 <TAB> param2
//
// TAB and the line breaks are stripped from every parameter before writing, so
// the format cannot be broken by anything the user types into an edit box. An
// absent or unreadable value means "no entries yet", and the loader then
// migrates the two legacy values ("Hotkey" / "HideHotkey") so an upgrading user
// keeps the combinations they already chose.
///////////////////////////////////////////////////////////////////////////////

#ifndef __HOTKEYLIST_H__
#define __HOTKEYLIST_H__

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

//Kinds of action. The numeric values are persisted, so never renumber them -
//only append.
#define NEKO_HT_HIDE       0    // hide / magnify / restore cycle
#define NEKO_HT_PROXY      1    // switch the Windows manual proxy
#define NEKO_HT_NOSLEEP    2    // keep the machine awake
#define NEKO_HT_WEBSITE    3    // open a page in a browser
#define NEKO_HT_PROJECTION 4    // flip between two display modes
#define NEKO_HT_TYPES      5

//Hard cap on entries. Each one costs a global registration and a row in the
//settings list; a runaway list is far more likely to be a bug than a wish.
#define NEKO_HT_MAX        24

//Parameter buffers. A browser command line can be long, so these are generous.
#define NEKO_HT_P1         512
#define NEKO_HT_P2         512

typedef struct tagNEKOHOTKEY
{
    DWORD dwType;                   // NEKO_HT_*
    DWORD dwHotkey;                 // low byte = virtual key, high byte = HOTKEYF_*
    char  szP1[NEKO_HT_P1];         // proxy: host      website: url       projection: mode A
    char  szP2[NEKO_HT_P2];         // proxy: port      website: browser   projection: mode B
} NEKOHOTKEY, *LPNEKOHOTKEY;

//The list itself, shared by the settings dialog (which edits it) and the pet
//(which registers the combinations). Defined in HotkeyList.cpp.
extern NEKOHOTKEY g_HotkeyList[NEKO_HT_MAX];
extern int        g_nHotkeyList;

//Read the list from the registry into the globals above, migrating the legacy
//two-slot values the first time. Always leaves the globals in a usable state
//(possibly empty) and returns the number of entries.
int  HotkeyListLoad();

//Write the globals above back to the registry. Returns FALSE when the write
//failed, in which case the old value is still in place.
BOOL HotkeyListSave();

//Remove one entry, shifting the rest down. Returns FALSE for a bad index.
BOOL HotkeyListRemove( int nIndex );

//Append an entry pre-filled with the defaults for its kind. Returns the new
//index, or -1 when the list is full. The caller is expected to let the user
//adjust the parameters afterwards.
int  HotkeyListAppend( DWORD dwType );

//Check the whole list before it is written. On failure the caller shows
//szError to the user. Two rules are enforced:
//  * every non-zero combination must carry a modifier (a bare key would be
//    stolen from every other program), and
//  * no two entries may share a combination, because the second registration
//    would silently fail and one action would become unreachable.
BOOL HotkeyListValidate( char* szError, int cbError );

//Default parameter values for a freshly added entry of the given kind.
void HotkeyListSetDefaults( LPNEKOHOTKEY lpEntry );

//Address used when the legacy single proxy shortcut is migrated into a list
//entry. It is deliberately the address the previous build was wired to, so an
//upgrading user keeps working, whereas a brand-new entry gets the neutral
//default the user asked for (192.168.1.1:7890, see HotkeyListSetDefaults).
#define NEKO_PROXY_MIGRATED_HOST "192.168.10.210"
#define NEKO_PROXY_MIGRATED_PORT "7890"

//Human-readable name of a kind, for the settings list and the tray hints.
const char* HotkeyTypeName( DWORD dwType );

//Projection-mode tables. Exposed because the dispatcher has to turn a stored
//mode back into the DisplaySwitch argument, and doing that from two places with
//two copies of the table is how the settings dialog and the hot key end up
//disagreeing about what "辅助" means.
//   index 0 屏幕1 -> /internal     index 1 屏幕2   -> /external
//   index 2 扩展  -> /extend       index 3 辅助    -> /clone
const char* HotkeyProjectionMode( int nIndex );     //stored name
const char* HotkeyProjectionSwitch( int nIndex );   //DisplaySwitch argument
int         HotkeyProjectionIndex( const char* szMode );  // -1 when unknown
#define NEKO_PROJECTION_MODES 4

//Build the one-line label shown in the "功能" column, e.g.
//"代理 192.168.10.210:7890" or "投影 扩展 ⇄ 仅屏幕1".
void HotkeyListLabel( const NEKOHOTKEY* lpEntry, char* szOut, int cbOut );

#endif //__HOTKEYLIST_H__
