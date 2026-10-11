/************************************
 
    "Start with Windows" support

    The pet is registered in the per-user
    Run key, which needs no administrator
    rights and can be removed by the user
    at any time from the settings dialog.

 *************************************/

#ifndef _AUTOSTART_HPP
#define _AUTOSTART_HPP

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

//value name used under HKCU\...\CurrentVersion\Run
#define NEKO_RUN_VALUE "Neko98"

//The file name of the pet. The Run entry must always name the PET, never the
//settings program, and this code is shared by both executables.
#define NEKO_PET_EXE "Neko98.exe"

//Is the pet currently registered to start with Windows?
BOOL NekoAutoStartIsEnabled();

//Add or remove the Run entry. Returns TRUE on success.
BOOL NekoAutoStartSet( BOOL bEnable );

//The entry is a plain quoted path, so if the user moves the folder the stored
//path goes stale. Called once at start-up, this repoints the entry at wherever
//the running Neko98.exe actually lives. Returns TRUE if anything was updated.
BOOL NekoAutoStartRepair();

#endif
