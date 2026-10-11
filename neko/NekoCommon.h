/************************************
 
   Common Definitions header

   Contains definitions used by both
   Neko95 and NekoCFG applications

 *************************************/

#ifndef _NEKOCOMMON_H
#define _NEKOCOMMON_H

//class & window names
#define szNekoClassName   "Neko95Class"
#define szNekoWindowTitle "Neko95"

//registry key for Neko's settings
#define szNekoRegKey      "Software\\DHSoft\\Neko95"

//sub-key names
#define szNekoTaskbarKey        "Taskbar Icon"

#define szNekoSpeedKey          "Speed"
#define szNekoSenseKey          "Sensitivity"
#define szNekoLibraryKey        "Library"
#define szNekoSndIdle1Key       "Snd_Idle1"
#define szNekoSndIdle2Key       "Snd_Idle2"
#define szNekoSndIdle3Key       "Snd_Idle3"
#define szNekoSndSleepKey       "Snd_Sleep"
#define szNekoSndAwakeKey       "Snd_Awake"
#define szNekoSndFreqKey        "Sound Frequency"
#define szNekoScaleKey          "Size"
#define szNekoOnTopKey          "On Top"
#define szNekoFootprintLibKey   "Footprint Library"
#define szNekoFootprintKey      "Footprints"

#define szNekoActionKey         "Action"

#define szNekoNumCatsKey        "NumNekos"

//Global hotkeys, stored in the Windows hotkey-control format: low byte = virtual
//key, high byte = HOTKEYF_* modifier flags. 0 means "no hotkey". When the value
//is absent altogether the pet falls back to the DEFAULT_* value below, so the
//feature works out of the box; writing 0 from the settings program disables it.
//Two independent combinations: one pauses/resumes the cats in place, the other
//hides them completely (and brings them back).
#define szNekoHotkeyKey         "Hotkey"        /* pause/resume  */
#define szNekoHideHotkeyKey     "HideHotkey"    /* hide/show     */
#define NEKO_DEFAULT_HOTKEY     0x064E   /* Ctrl+Alt+N - pause/resume */
#define NEKO_DEFAULT_HIDE_HOTKEY 0x0648  /* Ctrl+Alt+H - hide/show    */

//Pause/hidden state, so a /pause/ or /hide/ survives a restart of the pet.
#define szNekoPausedKey         "Paused"
#define szNekoHiddenKey         "Hidden"

#define HOTKEYF_SHIFT_           0x01
#define HOTKEYF_CONTROL_         0x02
#define HOTKEYF_ALT_             0x04
#define HOTKEYF_EXT_             0x08

//max number of cats we will ever instantiate (guards a corrupt/absurd registry)
#define MAX_NEKO_CATS           (20)


//identifiers for different Neko actions
#define CHASE_MOUSE             0
#define RUN_AWAY_FROM_MOUSE     1
#define RUN_AROUND_RANDOMLY     2
#define PACE_AROUND_SCREEN      3
#define RUN_AROUND              4


//message sent from NekoCFG to Neko to signal settings have changed
#define MY_UPDATENEKO (WM_USER+33)

//maximum length of a neko 'Name'
#define MAX_NEKO_NAME (64)

#endif
