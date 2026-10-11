// probe_dpi_aware.cpp - verify a binary's ACTUAL DPI awareness at runtime.
// The shipped Neko98.exe manifest declares <dpiAwareness>PerMonitorV2</dpiAwareness>
// inside xmlns="...SMI/2015..."; the canonical namespace is SMI/2016. If that
// namespace is wrong the element is ignored and awareness falls back to the
// sibling <dpiAware>true/true</dpiAware> = System aware. This probe settles it
// by asking the OS about the launched process instead of reading the XML.
#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *PFN_GetProcessDpiAwareness)(HANDLE, int*);
typedef BOOL    (WINAPI *PFN_GetProcessDpiAwarenessContext)(HANDLE, void**);

int main( int argc, char** argv )
{
    if( argc < 2 ) { printf("usage: probe_dpi_aware.exe <exe>\n"); return 1; }

    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    char cmd[MAX_PATH*2];
    wsprintfA( cmd, "\"%s\"", argv[1] );

    if( !CreateProcessA( NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ) )
    {
        printf("CreateProcess failed err=%lu\n", GetLastError());
        return 1;
    }
    printf("launched pid=%lu : %s\n", pi.dwProcessId, argv[1]);
    Sleep( 2500 );

    // CRITICAL: this program kills any previously running instance via
    // FindWindow + WM_QUIT and then returns FALSE. If another pet was already
    // running, the copy we just launched is already dead and any DPI answer
    // about it is meaningless. Report liveness before believing the result.
    DWORD ec = 0;
    GetExitCodeProcess( pi.hProcess, &ec );
    printf("  still running after 2.5s: %s (exit code %lu)\n",
           ec == STILL_ACTIVE ? "YES" : "NO -- measurement unreliable",
           (unsigned long)ec);

    HMODULE h = LoadLibraryA("shcore.dll");
    PFN_GetProcessDpiAwareness pGet = h
        ? (PFN_GetProcessDpiAwareness)GetProcAddress(h, "GetProcessDpiAwareness") : NULL;

    int aw = -1;
    if( pGet )
    {
        HRESULT hr = pGet( pi.hProcess, &aw );
        printf("  GetProcessDpiAwareness hr=0x%08lX value=%d\n", (unsigned long)hr, aw);
    }
    else printf("  GetProcessDpiAwareness unavailable\n");

    const char* nm = (aw == 0) ? "DPI_AWARENESS_UNAWARE"
                   : (aw == 1) ? "DPI_AWARENESS_SYSTEM_AWARE"
                   : (aw == 2) ? "DPI_AWARENESS_PER_MONITOR_AWARE"
                   : "??";
    printf("  => %s   (0=unaware 1=system 2=per-monitor)\n", nm);

    // Confirm the finer-grained context when available (V2 vs V1).
    HMODULE hu = GetModuleHandleA("user32.dll");
    PFN_GetProcessDpiAwarenessContext pCtx = hu
        ? (PFN_GetProcessDpiAwarenessContext)GetProcAddress(hu, "GetProcessDpiAwarenessContext") : NULL;
    if( pCtx )
    {
        void* ctx = NULL;
        BOOL ok = pCtx( pi.hProcess, &ctx );
        const char* cn = "?";
        if( ctx == (void*)(LONG_PTR)-1 ) cn = "UNAWARE";
        else if( ctx == (void*)(LONG_PTR)-2 ) cn = "SYSTEM_AWARE";
        else if( ctx == (void*)(LONG_PTR)-3 ) cn = "PER_MONITOR_AWARE";
        else if( ctx == (void*)(LONG_PTR)-4 ) cn = "PER_MONITOR_AWARE_V2";
        else if( ctx == (void*)(LONG_PTR)-5 ) cn = "UNAWARE_GDISCALED";
        printf("  awareness context ok=%d ctx=%p => %s\n", ok, ctx, cn);
    }

    TerminateProcess( pi.hProcess, 0 );
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );
    return 0;
}
