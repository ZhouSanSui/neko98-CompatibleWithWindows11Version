//probe_hide_loss - does a hidden layered window lose its content on re-show?
//
//WHY THIS EXISTS
//A live probe that toggles the pet with ShowWindow(SW_HIDE)/SW_SHOWNOACTIVATE while
//sampling reported "mean luminance 0" for a full 14-second watch in one run out of
//four. Zero painted pixels for 14 seconds is not measurement noise - it is the
//symptom the user reported ("程序在跑，小猫却不在桌面上"). This probe isolates it.
//
//THE QUESTION
//A layered window's content lives in whatever UpdateLayeredWindow last pushed. If
//hiding and re-showing discards that content, then a pet that is not currently
//repainting (asleep, STOP state, paused) would come back BLANK and stay blank until
//something makes it repaint. That is a plausible mechanism for the user's report, and
//it is testable without touching the product.
//
//METHOD
//   1. Launch the pet, wait until it is visible AND painting.
//   2. Hide it externally, wait, show it again.
//   3. Measure immediately (no repaint has been requested).
//   4. Sleep long enough for the pet's own 200 ms timer to tick many times, measure
//      again.
//If step 3 is empty and step 4 is painted, content is lost on hide and only the pet's
//own repaint brings it back - which means "how long it stays invisible depends on
//whether it happens to redraw". If both are painted, content survives and the blank
//watch in the other probe was something else.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_nFail = 0;

static void CHECK( const char* what, BOOL ok, const char* detail )
{
    printf( "[%s] %-56s %s\n", ok ? "PASS" : "FAIL", what, detail ? detail : "" );
    if( !ok ) g_nFail++;
}

static DWORD g_targetPid = 0;
static HWND  g_pet = NULL;

static BOOL CALLBACK FindPet( HWND hWnd, LPARAM )
{
    DWORD pid = 0;
    GetWindowThreadProcessId( hWnd, &pid );
    if( pid != g_targetPid ) return TRUE;

    char szClass[128] = {0};
    GetClassNameA( hWnd, szClass, sizeof(szClass) - 1 );
    if( strcmp( szClass, "Neko98TransparentPet" ) == 0 )
    {
        g_pet = hWnd;
        return FALSE;
    }
    return TRUE;
}

static HDC g_hScreen = NULL;

//Count the pixels the pet itself paints, by differencing the window rect against
//the same rect with the window hidden. Everything that differs is the pet's ink.
static int MeasurePetInk( int* pDiff )
{
    *pDiff = 0;

    g_pet = NULL;
    EnumWindows( FindPet, 0 );
    if( !g_pet ) return -1;                 //window gone
    if( !IsWindowVisible( g_pet ) ) return -2;   //not visible at all

    RECT rc;
    if( !GetWindowRect( g_pet, &rc ) ) return -3;
    int cx = rc.right - rc.left, cy = rc.bottom - rc.top;
    if( cx <= 0 || cy <= 0 ) return -4;

    if( !g_hScreen ) g_hScreen = GetDC( NULL );

    HDC hMem = CreateCompatibleDC( g_hScreen );
    if( !hMem ) return -5;

    BITMAPINFO bmi;
    memset( &bmi, 0, sizeof(bmi) );
    bmi.bmiHeader.biSize     = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth    = cx;
    bmi.bmiHeader.biHeight   = -cy;
    bmi.bmiHeader.biPlanes   = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pA = NULL;
    HBITMAP hA = CreateDIBSection( g_hScreen, &bmi, DIB_RGB_COLORS, &pA, NULL, 0 );
    if( !hA || !pA ) { DeleteDC( hMem ); return -6; }

    HGDIOBJ hOld = SelectObject( hMem, hA );
    BitBlt( hMem, 0, 0, cx, cy, g_hScreen, rc.left, rc.top, SRCCOPY );

    //Copy what we have, then hide and copy again.
    BYTE* pWith = (BYTE*)malloc( (size_t)cx * cy * 4 );
    if( !pWith ) { SelectObject( hMem, hOld ); DeleteObject( hA ); DeleteDC( hMem ); return -7; }
    memcpy( pWith, pA, (size_t)cx * cy * 4 );

    ShowWindow( g_pet, SW_HIDE );
    Sleep( 200 );
    BitBlt( hMem, 0, 0, cx, cy, g_hScreen, rc.left, rc.top, SRCCOPY );
    ShowWindow( g_pet, SW_SHOWNOACTIVATE );

    int diff = 0;
    for( int i = 0; i < cx * cy; i++ )
    {
        BYTE* a = pWith + (size_t)i * 4;
        BYTE* b = (BYTE*)pA + (size_t)i * 4;
        int d = abs( (int)a[0] - (int)b[0] ) +
                abs( (int)a[1] - (int)b[1] ) +
                abs( (int)a[2] - (int)b[2] );
        if( d >= 24 ) diff++;
    }

    free( pWith );
    SelectObject( hMem, hOld );
    DeleteObject( hA );
    DeleteDC( hMem );

    *pDiff = diff;
    return diff;
}

int main( int argc, char** argv )
{
    if( argc < 2 )
    {
        printf( "usage: probe_hide_loss.exe <Neko98.exe>\n" );
        return 1;
    }

    //Match the pet's DPI awareness so the rects agree.
    typedef BOOL (WINAPI *PFN_SET)(HANDLE);
    HMODULE hUser = LoadLibraryA( "user32.dll" );
    if( hUser )
    {
        PFN_SET pSet = (PFN_SET)GetProcAddress( hUser, "SetProcessDpiAwarenessContext" );
        if( pSet ) pSet( (HANDLE)(LONG_PTR)-4 );
        FreeLibrary( hUser );
    }

    char szCmd[1024];
    wsprintfA( szCmd, "\"%s\"", argv[1] );

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset( &si, 0, sizeof(si) );
    si.cb = sizeof(si);
    memset( &pi, 0, sizeof(pi) );

    if( !CreateProcessA( NULL, szCmd, NULL, NULL, FALSE,
                         DETACHED_PROCESS, NULL, NULL, &si, &pi ) )
    {
        printf( "cannot launch (error %lu)\n", GetLastError() );
        return 1;
    }
    g_targetPid = pi.dwProcessId;
    printf( "launched pid=%lu\n", pi.dwProcessId );

    Sleep( 2500 );

    //--- baseline: it must be painting before the experiment means anything ---
    int baseline = 0;
    for( int i = 0; i < 20 && baseline <= 30; i++ )
    {
        MeasurePetInk( &baseline );
        if( baseline <= 30 ) Sleep( 250 );
    }

    char buf[512];
    sprintf( buf, "%d px of pet ink before the experiment", baseline );
    CHECK( "the pet is painting before we start", baseline > 30, buf );

    if( baseline <= 30 )
    {
        DWORD dwExit = 0;
        if( GetExitCodeProcess( pi.hProcess, &dwExit ) && dwExit == STILL_ACTIVE )
            TerminateProcess( pi.hProcess, 0 );
        CloseHandle( pi.hThread ); CloseHandle( pi.hProcess );
        printf( "\n=== FAILED (baseline never established) ===\n" );
        return 2;
    }

    //--- the experiment ---------------------------------------------------
    //Hide, wait, show. NOTHING tells the pet to repaint; whatever it does next is
    //its own timer's doing.
    ShowWindow( g_pet, SW_HIDE );
    Sleep( 600 );
    ShowWindow( g_pet, SW_SHOWNOACTIVATE );

    //Measure immediately, before its next timer tick can land.
    int afterShow = 0;
    int rcImmediate = MeasurePetInk( &afterShow );
    printf( "     immediately after re-show: rc=%d ink=%d px\n", rcImmediate, afterShow );

    //Then give it a full second of its own 200 ms timer.
    Sleep( 1000 );
    int afterWait = 0;
    int rcWait = MeasurePetInk( &afterWait );
    printf( "     after 1000 ms of its own timer: rc=%d ink=%d px\n", rcWait, afterWait );

    //--- assertions -------------------------------------------------------
    //The real question: does the pet come back? Either immediately (content survived)
    //or within a second (it repainted on its own) is acceptable. Staying blank is not.
    sprintf( buf, "immediate=%d after 1s=%d (baseline %d)", afterShow, afterWait, baseline );
    CHECK( "the pet is visible again after being hidden externally", afterWait > 30, buf );

    sprintf( buf, "content survived re-show: immediate %d px of %d baseline",
             afterShow, baseline );
    printf( "     (%s)\n", buf );

    //--- clean up ---------------------------------------------------------
    Sleep( 300 );
    DWORD dwExit = 0;
    if( GetExitCodeProcess( pi.hProcess, &dwExit ) && dwExit == STILL_ACTIVE )
        TerminateProcess( pi.hProcess, 0 );
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );
    if( g_hScreen ) ReleaseDC( NULL, g_hScreen );

    printf( "\n=== %s (%d failures) ===\n", g_nFail ? "FAILED" : "ALL PASS", g_nFail );
    return g_nFail ? 2 : 0;
}
