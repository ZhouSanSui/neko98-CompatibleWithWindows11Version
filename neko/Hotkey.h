/************************************
 
    Global hotkey support

    The pet registers up to two
    system-wide hot keys so it can be
    paused, hidden or restored without
    touching the mouse. Both the key and
    the modifiers are chosen by the user
    in the settings dialog.

    Slot 0 pauses / resumes the cats in
    place. Slot 1 hides them completely
    (and brings them back). They are
    independent, so the user may bind
    only one, both or neither.

    Each value is stored in the registry
    as a single DWORD in the format the
    Win32 HOTKEY control uses: the low
    byte is the virtual key and the high
    byte holds the HOTKEYF_* modifier
    flags. That makes the value easy to
    inspect and keeps the pet and the
    settings program in agreement.

 *************************************/

#ifndef _HOTKEY_HPP
#define _HOTKEY_HPP

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

//Identify our hot keys to the system. The ids only have to be unique inside this
//process, and the slots must stay in step with NekoHotkeyLoad/Save below.
#define NEKO_HOTKEY_ID      1   /* slot 0: pause / resume */
#define NEKO_HIDE_HOTKEY_ID 2   /* slot 1: hide  / show   */

//Read a hot key from the registry. When the value is missing altogether the
//built-in default for that slot is returned, so the feature works before the user
//ever opens the settings dialog. A stored value of 0 means "the user disabled
//it". bHide selects the slot: FALSE = pause/resume, TRUE = hide/show.
DWORD NekoHotkeyLoad( BOOL bHide = FALSE );

//Persist a hot key. 0 disables it. bHide selects the slot.
BOOL NekoHotkeySave( DWORD dwHotkey, BOOL bHide = FALSE );

//(Re)register one hot key with the given window. Any previously registered hot
//key in that slot is released first. Passing 0 simply unregisters. Returns TRUE
//when the hot key is active afterwards. Another application may already own the
//combination, in which case registration fails and the caller should tell the
//user. bHide selects the slot.
BOOL NekoHotkeyApply( HWND hWnd, DWORD dwHotkey, BOOL bHide = FALSE );

//Release one hot key, or both when bAll is TRUE.
void NekoHotkeyRelease( HWND hWnd, BOOL bHide = FALSE );
void NekoHotkeyReleaseAll( HWND hWnd );

//Get a hot key currently registered for this window (0 when none).
DWORD NekoHotkeyCurrent( BOOL bHide = FALSE );

//Render a hot key as human readable text, e.g. "Ctrl+Alt+N", using the names in
//the currently installed keyboard layout. Always NUL terminates.
void NekoHotkeyFormat( DWORD dwHotkey, char* pszOut, int cbOut );

//True when the two slots hold the SAME combination. Registering both would make
//the second one fail (or, worse, make which action fires depend on registration
//order), so the settings dialog rejects it and the pet drops the duplicate.
BOOL NekoHotkeyConflict( DWORD dwPauseKey, DWORD dwHideKey );

//Turn a Win32 HOTKEY control's value into the same DWORD layout used above.
//The HOTKEY control already stores exactly vk | (modifiers << 8), so this is a
//thin wrapper that only normalises invalid combinations (a modifier-only key or
//no key at all becomes 0).
DWORD NekoHotkeyFromControl( WORD wHotkey );

//True when the value describes a usable combination (a real key, at least one
//modifier). The pet refuses to register a bare key because that would swallow
//that key for the whole desktop.
BOOL NekoHotkeyIsUsable( DWORD dwHotkey );

#endif
