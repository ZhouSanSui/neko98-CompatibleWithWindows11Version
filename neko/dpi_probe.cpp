// dpi_probe.cpp - ground truth about this machine's displays and DPI.
// Runs DPI-aware (PerMonitorV2) exactly like the shipped pet, then reports the
// metrics the pet actually sees so sizing decisions are based on facts.

#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *PFN_GetDpiForMonitor)( HMONITOR, int, UINT*, UINT* );
typedef BOOL (WINAPI *PFN_SetCtx)( void* );

static DWORD g_idx = 0;

static BOOL CALLBACK MonProc( HMONITOR hm, HDC hdc, LPRECT prc, LPARAM lp )
{
    PFN_GetDpiForMonitor pGet = (PFN_GetDpiForMonitor)lp;

    MONITORINFOEXA mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoA( hm, &mi );

    UINT dx = 0, dy = 0;
    if( pGet ) pGet( hm, 0, &dx, &dy );   //0 = MDT_EFFECTIVE_DPI

    printf( "  monitor %lu %-16s rect=%ld,%ld %ldx%ld  primary=%d  dpi=%ux%u (scale %.0f%%)\n",
            ++g_idx, mi.szDevice,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            (mi.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0,
            dx, dy, dx ? dx * 100.0 / 96.0 : 0.0 );
    return TRUE;
}

int main()
{
    //become PerMonitorV2 aware, like the shipped binary's manifest requests
    HMODULE user32 = GetModuleHandleA( "user32.dll" );
    PFN_SetCtx pSet = (PFN_SetCtx)GetProcAddress( user32, "SetProcessDpiAwarenessContext" );
    if( pSet )
    {
        //DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
        if( pSet( (void*)(LONG_PTR)-4 ) ) printf( "SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2) OK\n" );
        else                              printf( "SetProcessDpiAwarenessContext failed, err=%lu\n", GetLastError() );
    }
    else printf( "SetProcessDpiAwarenessContext unavailable\n" );

    printf( "\nDPI-aware view (what the pet sees):\n" );
    printf( "  SM_CXVIRTUALSCREEN=%d SM_CYVIRTUALSCREEN=%d  origin=%d,%d\n",
            GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
            GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN) );
    printf( "  SM_CXSCREEN=%d SM_CYSCREEN=%d\n",
            GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) );

    HDC screen = GetDC(NULL);
    printf( "  primary DC LOGPIXELSX=%d LOGPIXELSY=%d\n",
            GetDeviceCaps(screen, LOGPIXELSX), GetDeviceCaps(screen, LOGPIXELSY) );
    ReleaseDC( NULL, screen );

    HMODULE shcore = LoadLibraryA( "shcore.dll" );
    PFN_GetDpiForMonitor pGet = shcore ? (PFN_GetDpiForMonitor)GetProcAddress( shcore, "GetDpiForMonitor" ) : NULL;
    printf( "\nmonitors:\n" );
    EnumDisplayMonitors( NULL, NULL, MonProc, (LPARAM)pGet );

    return 0;
}
