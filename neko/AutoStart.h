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

//The settings program. An auto-start entry naming it is never valid, so a
//stored value that points at it is migrated to the pet.
#define NEKO_SETTINGS_EXE "NekoCFG.exe"

//Is the pet currently registered to start with Windows?
BOOL NekoAutoStartIsEnabled();

//Add or remove the Run entry. Returns TRUE on success.
BOOL NekoAutoStartSet( BOOL bEnable );

//The entry is a plain quoted path, so if the user moves the folder the stored
//path goes stale. Called once at start-up, this repoints the entry at wherever
//the running Neko98.exe actually lives - but ONLY when the stored path no
//longer resolves to a real pet file. A working entry is left alone, so running
//a copy from another folder cannot hijack the user's auto-start setting.
//Returns TRUE if anything was updated.
BOOL NekoAutoStartRepair();

#endif
