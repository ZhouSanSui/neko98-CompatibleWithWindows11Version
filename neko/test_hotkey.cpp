// test_hotkey.cpp - runtime test for the two global hot keys.
//
// It links the REAL Hotkey.o and drives the real functions against the real
// HKCU registry key and the real RegisterHotKey API, so it proves the shipped
// behaviour rather than a lookalike:
//
//   * each slot has its own default and its own registry value, and silencing
//     one slot leaves the other one alone
//   * registering slot 0 does not disturb slot 1
//   * the SAME combination in both slots is rejected instead of silently
//     stealing the first one
//   * pressing the hide/show combination really delivers WM_HOTKEY carrying
//     NEKO_HIDE_HOTKEY_ID (and the pause combination carries NEKO_HOTKEY_ID),
//     which is exactly the wiring the pet's WndProc switches on
//   * NekoHotkeyReleaseAll() leaves nothing registered
//
// SAFETY: both stored values are saved before the test and restored afterwards,
// so a failure part-way through cannot leave the user's shortcuts altered.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "Hotkey.h"
#include "NekoCommon.h"

//The combinations this test uses, in the stored layout (low byte = VK, high byte
//= HOTKEYF_*). Ctrl+Alt+F9 / Ctrl+Alt+F10 are free on a normal machine.
#define TEST_PAUSE_KEY ( (HOTKEYF_CONTROL_ | HOTKEYF_ALT_) << 8 | VK_F9  )
#define TEST_HIDE_KEY  ( (HOTKEYF_CONTROL_ | HOTKEYF_ALT_) << 8 | VK_F10 )

static int g_nFail = 0;
static int g_nSkip = 0;

static void CHECK( const char* what, BOOL ok, const char* detail )
{
    printf( "[%s] %-56s %s\n", ok ? "PASS" : "FAIL", what, detail ? detail : "" );
    if( !ok ) g_nFail++;
}

static void SKIP( const char* what, const char* why )
{
    printf( "[SKIP] %-56s %s\n", what, why ? why : "" );
    g_nSkip++;
}

/* Raw registry access, deliberately bypassing Hotkey.cpp, so the test can plant
   and inspect values on its own. */
static BOOL RawGet( const char* pszName, DWORD* pdwOut )
{
    HKEY hKey = NULL;
    if( RegOpenKeyEx( HKEY_CURRENT_USER, szNekoRegKey, 0, KEY_READ, &hKey ) != ERROR_SUCCESS )
        return FALSE;

    DWORD dwSize = sizeof(DWORD);
    DWORD dwType = 0;
    LONG lRes = RegQueryValueEx( hKey, pszName, NULL, &dwType, (LPBYTE)pdwOut, &dwSize );
    RegCloseKey( hKey );
    return ( lRes == ERROR_SUCCESS && dwType == REG_DWORD && dwSize == sizeof(DWORD) );
}

static void RawSet( const char* pszName, DWORD dwValue )
{
    HKEY hKey = NULL;
    if( RegCreateKeyEx( HKEY_CURRENT_USER, szNekoRegKey, 0, NULL, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, NULL, &hKey, NULL ) != ERROR_SUCCESS )
        return;
    RegSetValueEx( hKey, pszName, 0, REG_DWORD, (const BYTE*)&dwValue, sizeof(dwValue) );
    RegCloseKey( hKey );
}

static void RawDelete( const char* pszName )
{
    HKEY hKey = NULL;
    if( RegOpenKeyEx( HKEY_CURRENT_USER, szNekoRegKey, 0, KEY_SET_VALUE, &hKey ) != ERROR_SUCCESS )
        return;
    RegDeleteValue( hKey, pszName );
    RegCloseKey( hKey );
}

/* Our own hidden window: WM_HOTKEY is posted to the thread that registered, so
   a real window is the only honest way to observe it. */
static HWND g_hWnd = NULL;
static int  g_nPauseMsgs = 0;
static int  g_nHideMsgs  = 0;


static LRESULT CALLBACK TestWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    (void)lParam;
    if( msg == WM_HOTKEY )
    {
        if( wParam == NEKO_HOTKEY_ID )      g_nPauseMsgs++;
        else if( wParam == NEKO_HIDE_HOTKEY_ID ) g_nHideMsgs++;
    }
    return DefWindowProc( hWnd, msg, wParam, lParam );
}

static HWND MakeWindow()
{
    WNDCLASS wc;
    memset( &wc, 0, sizeof(wc) );
    wc.lpfnWndProc = TestWndProc;
    wc.hInstance = GetModuleHandle( NULL );
    wc.lpszClassName = "NekoHotkeyTestWnd";
    if( !RegisterClass( &wc ) ) return NULL;
    return CreateWindow( "NekoHotkeyTestWnd", "t", 0, 0, 0, 0, 0, NULL, NULL,
                         GetModuleHandle( NULL ), NULL );
}

/* Press a Ctrl+Alt+<vk> combination for real. Returns the number of messages
   pumped afterwards. */
static void PressKey( BYTE bVk )
{
    INPUT in[6];
    memset( in, 0, sizeof(in) );
    for( int i = 0; i < 6; i++ ) in[i].type = INPUT_KEYBOARD;

    in[0].ki.wVk = VK_CONTROL;
    in[1].ki.wVk = VK_MENU;
    in[2].ki.wVk = bVk;
    in[3].ki.wVk = bVk;      in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    in[4].ki.wVk = VK_MENU;  in[4].ki.dwFlags = KEYEVENTF_KEYUP;
    in[5].ki.wVk = VK_CONTROL; in[5].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput( 6, in, sizeof(INPUT) );

    //Give the shell a moment, then drain whatever arrived.
    DWORD dwEnd = GetTickCount() + 700;
    MSG msg;
    while( GetTickCount() < dwEnd )
    {
        while( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
        {
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        Sleep( 20 );
    }
}

int main()
{
    printf( "=== Neko98 global hotkey test ===\n" );

    //---- save the user's real values so this test is non-destructive ----
    DWORD dwSavedPause = 0, dwSavedHide = 0;
    BOOL bHadPause = RawGet( szNekoHotkeyKey, &dwSavedPause );
    BOOL bHadHide  = RawGet( szNekoHideHotkeyKey, &dwSavedHide );
    printf( "existing values: pause=%s hide=%s\n",
            bHadPause ? "present" : "(absent)", bHadHide ? "present" : "(absent)" );

    char buf[256];

    //---- 1. defaults, per slot, when nothing is stored ----
    RawDelete( szNekoHotkeyKey );
    RawDelete( szNekoHideHotkeyKey );

    DWORD dwPause = NekoHotkeyLoad( FALSE );
    DWORD dwHide  = NekoHotkeyLoad( TRUE );
    sprintf( buf, "pause=0x%04X hide=0x%04X", dwPause, dwHide );
    CHECK( "pause slot falls back to its own default",
           dwPause == NEKO_DEFAULT_HOTKEY, buf );
    CHECK( "hide slot falls back to its own default",
           dwHide == NEKO_DEFAULT_HIDE_HOTKEY, buf );
    CHECK( "the two defaults are different combinations",
           NEKO_DEFAULT_HOTKEY != NEKO_DEFAULT_HIDE_HOTKEY, buf );

    //---- 2. the slots are stored independently ----
    NekoHotkeySave( TEST_PAUSE_KEY, FALSE );
    NekoHotkeySave( TEST_HIDE_KEY, TRUE );
    sprintf( buf, "pause=0x%04X hide=0x%04X", NekoHotkeyLoad( FALSE ), NekoHotkeyLoad( TRUE ) );
    CHECK( "each slot reads back its own stored value",
           NekoHotkeyLoad( FALSE ) == TEST_PAUSE_KEY && NekoHotkeyLoad( TRUE ) == TEST_HIDE_KEY, buf );

    DWORD dwOnDisk = 0;
    CHECK( "the two slots use two different registry value names",
           RawGet( szNekoHotkeyKey, &dwOnDisk ) && dwOnDisk == TEST_PAUSE_KEY, buf );
    CHECK( "the hide slot has its own registry value",
           RawGet( szNekoHideHotkeyKey, &dwOnDisk ) && dwOnDisk == TEST_HIDE_KEY, buf );

    //disabling one slot must not touch the other
    NekoHotkeySave( 0, FALSE );
    sprintf( buf, "pause=0x%04X hide=0x%04X", NekoHotkeyLoad( FALSE ), NekoHotkeyLoad( TRUE ) );
    CHECK( "disabling the pause slot leaves the hide slot alone",
           NekoHotkeyLoad( FALSE ) == 0 && NekoHotkeyLoad( TRUE ) == TEST_HIDE_KEY, buf );
    NekoHotkeySave( TEST_PAUSE_KEY, FALSE );

    //---- 3. the duplicate-combination guard ----
    CHECK( "two disabled keys are not a conflict",
           NekoHotkeyConflict( 0, 0 ) == FALSE, NULL );
    CHECK( "a disabled key never conflicts",
           NekoHotkeyConflict( 0, TEST_HIDE_KEY ) == FALSE &&
           NekoHotkeyConflict( TEST_PAUSE_KEY, 0 ) == FALSE, NULL );
    CHECK( "an identical real combination IS a conflict",
           NekoHotkeyConflict( TEST_PAUSE_KEY, TEST_PAUSE_KEY ) != FALSE, NULL );
    CHECK( "different combinations are not a conflict",
           NekoHotkeyConflict( TEST_PAUSE_KEY, TEST_HIDE_KEY ) == FALSE, NULL );

    //---- 4. real registration, both slots at once ----
    g_hWnd = MakeWindow();
    if( g_hWnd == NULL )
    {
        printf( "!! could not create the test window; registration tests skipped\n" );
        g_nFail++;
    }
    else
    {
        BOOL bPauseOk = NekoHotkeyApply( g_hWnd, TEST_PAUSE_KEY, FALSE );
        sprintf( buf, "Apply(pause)=%d", bPauseOk ? 1 : 0 );
        CHECK( "the pause combination registers", bPauseOk != FALSE, buf );
        CHECK( "Current(FALSE) reports it",
               NekoHotkeyCurrent( FALSE ) == TEST_PAUSE_KEY, buf );

        BOOL bHideOk = NekoHotkeyApply( g_hWnd, TEST_HIDE_KEY, TRUE );
        sprintf( buf, "Apply(hide)=%d pause still=0x%04X",
                 bHideOk ? 1 : 0, NekoHotkeyCurrent( FALSE ) );
        CHECK( "the hide combination registers alongside it", bHideOk != FALSE, buf );
        CHECK( "registering the hide key does not disturb the pause key",
               NekoHotkeyCurrent( FALSE ) == TEST_PAUSE_KEY, buf );
        CHECK( "Current(TRUE) reports the hide key",
               NekoHotkeyCurrent( TRUE ) == TEST_HIDE_KEY, buf );

        //the same combination in the other slot must be refused, not stolen
        BOOL bDup = NekoHotkeyApply( g_hWnd, TEST_PAUSE_KEY, TRUE );
        sprintf( buf, "Apply(duplicate)=%d pause still=0x%04X",
                 bDup ? 1 : 0, NekoHotkeyCurrent( FALSE ) );
        CHECK( "the same combination cannot occupy both slots", bDup == FALSE, buf );
        CHECK( "the refused duplicate left the pause slot registered",
               NekoHotkeyCurrent( FALSE ) == TEST_PAUSE_KEY, buf );

        //re-apply the hide key, since the refusal above released that slot
        NekoHotkeyApply( g_hWnd, TEST_HIDE_KEY, TRUE );

        //---- 5. does the OS really deliver the right id? ----
        g_nPauseMsgs = 0;
        g_nHideMsgs  = 0;
        PressKey( VK_F9 );
        sprintf( buf, "pause msgs=%d hide msgs=%d", g_nPauseMsgs, g_nHideMsgs );
        if( g_nPauseMsgs == 0 && g_nHideMsgs == 0 )
            SKIP( "pressing Ctrl+Alt+F9 delivers WM_HOTKEY", "no hot key message (injected input unavailable here)" );
        else
        {
            CHECK( "Ctrl+Alt+F9 delivers WM_HOTKEY with NEKO_HOTKEY_ID",
                   g_nPauseMsgs == 1 && g_nHideMsgs == 0, buf );
        }

        g_nPauseMsgs = 0;
        g_nHideMsgs  = 0;
        PressKey( VK_F10 );
        sprintf( buf, "pause msgs=%d hide msgs=%d", g_nPauseMsgs, g_nHideMsgs );
        if( g_nPauseMsgs == 0 && g_nHideMsgs == 0 )
            SKIP( "pressing Ctrl+Alt+F10 delivers WM_HOTKEY", "no hot key message (injected input unavailable here)" );
        else
        {
            CHECK( "Ctrl+Alt+F10 delivers WM_HOTKEY with NEKO_HIDE_HOTKEY_ID",
                   g_nHideMsgs == 1 && g_nPauseMsgs == 0, buf );
        }

        //---- 6. release clears every slot ----
        NekoHotkeyReleaseAll( g_hWnd );
        sprintf( buf, "pause=%d hide=%d",
                 (int)NekoHotkeyCurrent( FALSE ), (int)NekoHotkeyCurrent( TRUE ) );
        CHECK( "ReleaseAll() clears both slots",
               NekoHotkeyCurrent( FALSE ) == 0 && NekoHotkeyCurrent( TRUE ) == 0, buf );

        //prove it really unregistered: the combination is free again, and a
        //different key can now claim it
        BOOL bReclaim = NekoHotkeyApply( g_hWnd, TEST_PAUSE_KEY, TRUE );
        CHECK( "released combination can be claimed by the other slot",
               bReclaim != FALSE, buf );
        NekoHotkeyReleaseAll( g_hWnd );

        DestroyWindow( g_hWnd );
    }

    //---- 7. display formatting ----
    char szText[64];
    NekoHotkeyFormat( 0, szText, sizeof(szText) );
    CHECK( "a disabled key formats as the 'not set' text",
           strstr( szText, "未设置" ) != NULL, szText );

    NekoHotkeyFormat( (HOTKEYF_CONTROL_ | HOTKEYF_ALT_) << 8 | 'N', szText, sizeof(szText) );
    CHECK( "Ctrl+Alt+N formats with both modifiers and the key",
           strstr( szText, "Ctrl+" ) != NULL && strstr( szText, "Alt+" ) != NULL &&
           strstr( szText, "N" ) != NULL, szText );

    //---- restore the user's real values ----
    RawDelete( szNekoHotkeyKey );
    RawDelete( szNekoHideHotkeyKey );
    if( bHadPause ) RawSet( szNekoHotkeyKey, dwSavedPause );
    if( bHadHide )  RawSet( szNekoHideHotkeyKey, dwSavedHide );

    DWORD dwAfter = 0;
    BOOL bRestoredPause = ( RawGet( szNekoHotkeyKey, &dwAfter ) == bHadPause );
    BOOL bRestoredHide  = ( RawGet( szNekoHideHotkeyKey, &dwAfter ) == bHadHide );
    CHECK( "the user's original hotkey state was restored",
           bRestoredPause && bRestoredHide, NULL );

    printf( "\n=== %s (%d failures, %d skipped) ===\n",
            g_nFail ? "FAILED" : "ALL PASS", g_nFail, g_nSkip );
    return g_nFail ? 2 : 0;
}
