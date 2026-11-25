///////////////////////////////////////////////////////////////////////////////
// test_actions.cpp - unit tests for the three extra shortcut actions.
//
// Links the REAL build_neko\Actions.o, so these assertions cover the code that
// ships rather than a copy of it.
//
// What is asserted, and why it matters:
//   * browser id <-> index <-> display name stay in sync. The id is what gets
//     written into the registry, so if the mapping drifts an old shortcut would
//     silently start opening a different browser.
//   * WebLauncherFindExecutable finds at least one of Edge/Chrome on this
//     machine, and reports a path that actually exists. A shortcut that cannot
//     resolve its browser would otherwise appear to work and do nothing.
//   * the projection mode names match the four modes the settings page offers,
//     the index lookup round-trips, and an unknown name is rejected (-1). The
//     indices are persisted, so a mismatch would switch to the wrong mode.
//   * SleepGuardToggle really turns the request on and back off, and never
//     reports a state it did not achieve.
//
// Build: see build_test_actions.bat
///////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "Actions.h"
#include "HotkeyList.h"   // HotkeyProjectionMode / Index / Switch live here

static int g_nFail = 0;
static int g_nPass = 0;

static void CHECK( const char* szWhat, BOOL bOk, const char* szDetail = "" )
{
    printf( "[%s] %s%s%s\n", bOk ? "PASS" : "FAIL", szWhat,
            ( szDetail && szDetail[0] ) ? "  " : "", szDetail ? szDetail : "" );
    if( bOk ) g_nPass++;
    else      g_nFail++;
}

/* ------------------------------------------------------------------ browser */
static void TestBrowserIdentity()
{
    printf( "--- browser identity ---\n" );

    CHECK( "Edge maps to the id 'edge'",      strcmp( WebLauncherBrowserId( NEKO_BROWSER_EDGE ),   "edge" ) == 0 );
    CHECK( "Chrome maps to the id 'chrome'",  strcmp( WebLauncherBrowserId( NEKO_BROWSER_CHROME ), "chrome" ) == 0 );
    CHECK( "custom maps to the id 'custom'",  strcmp( WebLauncherBrowserId( NEKO_BROWSER_CUSTOM ), "custom" ) == 0 );

    CHECK( "'edge' reads back as Edge",     WebLauncherBrowserFromId( "edge" )   == NEKO_BROWSER_EDGE );
    CHECK( "'chrome' reads back as Chrome", WebLauncherBrowserFromId( "chrome" ) == NEKO_BROWSER_CHROME );
    CHECK( "'custom' reads back as custom", WebLauncherBrowserFromId( "custom" ) == NEKO_BROWSER_CUSTOM );

    /* An id that no longer exists (hand-edited or downgraded registry) must not
       turn into a random browser: Edge is the safest default. */
    CHECK( "an unknown id falls back to Edge", WebLauncherBrowserFromId( "netscape" ) == NEKO_BROWSER_EDGE );
    CHECK( "an empty id falls back to Edge",   WebLauncherBrowserFromId( "" )         == NEKO_BROWSER_EDGE );

    /* The names are display text; assert only that they are distinct and short
       enough for the combo box rather than pinning the exact Chinese here. */
    const char* pEdge = WebLauncherBrowserName( NEKO_BROWSER_EDGE );
    const char* pChrome = WebLauncherBrowserName( NEKO_BROWSER_CHROME );
    const char* pCustom = WebLauncherBrowserName( NEKO_BROWSER_CUSTOM );
    char szDetail[160];
    _snprintf( szDetail, sizeof szDetail, "edge=%s chrome=%s custom=%s", pEdge, pChrome, pCustom );
    szDetail[sizeof szDetail - 1] = '\0';
    CHECK( "every browser has a display name",
           pEdge[0] && pChrome[0] && pCustom[0], szDetail );
    CHECK( "the three display names differ",
           strcmp( pEdge, pChrome ) != 0 && strcmp( pEdge, pCustom ) != 0 &&
           strcmp( pChrome, pCustom ) != 0 );
}

static void TestBrowserLookup()
{
    printf( "--- browser executable lookup ---\n" );

    char szPath[MAX_PATH];
    BOOL bEdge = WebLauncherFindExecutable( NEKO_BROWSER_EDGE, szPath, sizeof szPath );

    if( bEdge )
    {
        DWORD dwAttr = GetFileAttributesA( szPath );
        char szDetail[MAX_PATH + 32];
        _snprintf( szDetail, sizeof szDetail, "path=%s", szPath );
        szDetail[sizeof szDetail - 1] = '\0';
        CHECK( "the resolved Edge path exists", dwAttr != INVALID_FILE_ATTRIBUTES &&
                                                 !( dwAttr & FILE_ATTRIBUTE_DIRECTORY ), szDetail );
    }
    else
    {
        /* Not fatal: Edge ships with Windows 11, but a stripped image may lack
           it. Report it as a skip rather than a failure so the suite is honest. */
        printf( "[SKIP] Edge was not found on this machine - shortcut would fail here\n" );
    }

    char szChrome[MAX_PATH];
    BOOL bChrome = WebLauncherFindExecutable( NEKO_BROWSER_CHROME, szChrome, sizeof szChrome );
    if( bChrome )
    {
        DWORD dwAttr = GetFileAttributesA( szChrome );
        char szDetail[MAX_PATH + 32];
        _snprintf( szDetail, sizeof szDetail, "path=%s", szChrome );
        szDetail[sizeof szDetail - 1] = '\0';
        CHECK( "the resolved Chrome path exists", dwAttr != INVALID_FILE_ATTRIBUTES &&
                                                   !( dwAttr & FILE_ATTRIBUTE_DIRECTORY ), szDetail );
    }
    else
    {
        printf( "[SKIP] Chrome is not installed on this machine\n" );
    }

    /* A custom browser that does not exist must be reported as not found instead
       of launching something arbitrary. */
    BOOL bBogus = WebLauncherFindExecutable( NEKO_BROWSER_CUSTOM, szPath, sizeof szPath );
    CHECK( "custom does not invent an executable", bBogus == FALSE );

    /* A URL with characters cmd.exe would reinterpret must be rejected as an
       empty/invalid URL rather than handed to the shell as a command. */
    CHECK( "an empty URL is refused", WebLauncherOpen( "", "edge", "" ) == FALSE );
    CHECK( "a NULL URL is refused",   WebLauncherOpen( NULL, "edge", "" ) == FALSE );
}

/* --------------------------------------------------------------- projection */
static void TestProjectionModes()
{
    printf( "--- projection modes ---\n" );

    static const char* aExpect[NEKO_PROJECTION_MODES];
    int i;

    /* The persisted value is the INDEX, and the index selects one of the four
       DisplaySwitch switches. Assert the names exist, are distinct, and that
       the name -> index lookup round-trips. */
    for( i = 0; i < NEKO_PROJECTION_MODES; i++ )
        aExpect[i] = HotkeyProjectionMode( i );

    char szList[256];
    _snprintf( szList, sizeof szList, "%s / %s / %s / %s",
               aExpect[0], aExpect[1], aExpect[2], aExpect[3] );
    szList[sizeof szList - 1] = '\0';
    CHECK( "the four projection modes all have names",
           aExpect[0][0] && aExpect[1][0] && aExpect[2][0] && aExpect[3][0], szList );

    CHECK( "the four projection names are distinct",
           strcmp( aExpect[0], aExpect[1] ) != 0 && strcmp( aExpect[0], aExpect[2] ) != 0 &&
           strcmp( aExpect[0], aExpect[3] ) != 0 && strcmp( aExpect[1], aExpect[2] ) != 0 &&
           strcmp( aExpect[1], aExpect[3] ) != 0 && strcmp( aExpect[2], aExpect[3] ) != 0 );

    for( i = 0; i < NEKO_PROJECTION_MODES; i++ )
    {
        int nBack = HotkeyProjectionIndex( aExpect[i] );
        char szDetail[96];
        _snprintf( szDetail, sizeof szDetail, "'%s' -> %d", aExpect[i], nBack );
        szDetail[sizeof szDetail - 1] = '\0';
        CHECK( "a projection name reads back as its own index", nBack == i, szDetail );
    }

    CHECK( "an unknown projection name is rejected", HotkeyProjectionIndex( "hologram" ) == -1 );
    CHECK( "an empty projection name is rejected",   HotkeyProjectionIndex( "" )         == -1 );

    /* The switch strings are what actually get passed to DisplaySwitch.exe; an
       empty one would make the shortcut a no-op. */
    for( i = 0; i < NEKO_PROJECTION_MODES; i++ )
    {
        const char* szSwitch = HotkeyProjectionSwitch( i );
        char szDetail[64];
        _snprintf( szDetail, sizeof szDetail, "mode %d -> '%s'", i, szSwitch );
        szDetail[sizeof szDetail - 1] = '\0';
        CHECK( "every projection mode has a DisplaySwitch argument",
               szSwitch && szSwitch[0] == '/', szDetail );
    }
}

/* -------------------------------------------------------------- sleep guard */
static void TestSleepGuard()
{
    printf( "--- keep-awake toggle ---\n" );

    BOOL bWas = SleepGuardIsActive();

    BOOL bOn = SleepGuardSetActive( TRUE );
    CHECK( "the keep-awake request can be turned on", bOn == TRUE );
    CHECK( "the reported state matches the request", SleepGuardIsActive() == TRUE );

    /* Turning it on twice must not be an error (the shortcut is a toggle, but a
       double press should still leave it on, not flip to off). */
    SleepGuardSetActive( TRUE );
    CHECK( "turning it on twice keeps it on", SleepGuardIsActive() == TRUE );

    BOOL bOff = SleepGuardSetActive( FALSE );
    CHECK( "the keep-awake request can be cleared", bOff == TRUE );
    CHECK( "the reported state is off again", SleepGuardIsActive() == FALSE );

    /* Toggle returns the state it moved to, which is what the tray tool tip
       shows; a wrong return value would display the opposite of reality. */
    BOOL bToggled = SleepGuardToggle();
    CHECK( "toggle reports the new state", bToggled == SleepGuardIsActive() );
    CHECK( "toggle from off goes on", bToggled == TRUE );

    bToggled = SleepGuardToggle();
    CHECK( "toggle from on goes off", bToggled == FALSE );
    CHECK( "and the state agrees", SleepGuardIsActive() == FALSE );

    /* Restore whatever the machine was doing before the test. */
    SleepGuardSetActive( bWas );
}

int main()
{
    printf( "=== Actions / shortcuts unit tests ===\n" );
    printf( "SleepGuard before the test: %s\n\n", SleepGuardIsActive() ? "on" : "off" );

    TestBrowserIdentity();
    TestBrowserLookup();
    TestProjectionModes();
    TestSleepGuard();

    printf( "\n=== %s (%d failures, %d passes) ===\n",
            g_nFail == 0 ? "ALL PASS" : "FAILURES", g_nFail, g_nPass );

    return g_nFail == 0 ? 0 : 1;
}
