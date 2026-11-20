// probe_zorder.cpp — is the pet REALLY the topmost window?
//
// WS_EX_TOPMOST in the exstyle only declares the INTENT.  The actual Z-order can
// be lost when another topmost window appears, when DWM restarts, when the
// virtual desktop changes, or when the shell re-stacks windows.  A pet whose
// topmost intent silently lapsed still reports IsWindowVisible()==TRUE and is
// still "on screen" — it is just BURIED under other windows, which looks exactly
// like "the program runs but the cat is gone".
//
// This probe walks the real Z-order from the front of the desktop and reports
// the index of the pet among all topmost windows, plus who sits above it.
//
// Build: g++ -O2 -o probe_zorder.exe probe_zorder.cpp -luser32 -lgdi32
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD g_pid    = 0;
static HWND  g_target = NULL;

static BOOL CALLBACK FindProc( HWND hWnd, LPARAM )
{
    if( g_target ) return FALSE;
    DWORD dwPid = 0;
    GetWindowThreadProcessId( hWnd, &dwPid );
    if( dwPid != g_pid ) return TRUE;
    char szClass[256] = {0};
    GetClassNameA( hWnd, szClass, sizeof szClass - 1 );
    if( strstr( szClass, "TransparentPet" ) == NULL ) return TRUE;
    g_target = hWnd;
    return FALSE;
}

int main( int argc, char** argv )
{
    if( argc < 2 ) { printf( "usage: probe_zorder.exe <pid>\n" ); return 2; }
    g_pid = (DWORD) atol( argv[1] );

    typedef BOOL (WINAPI *PFN_SET)(HANDLE);
    HMODULE hUser = LoadLibraryA( "user32.dll" );
    if( hUser )
    {
        PFN_SET pSet = (PFN_SET) GetProcAddress( hUser, "SetProcessDpiAwarenessContext" );
        if( pSet ) pSet( (HANDLE)(LONG_PTR)-4 );
    }

    EnumWindows( FindProc, 0 );
    if( !g_target ) { printf( "no pet window\n" ); return 1; }

    RECT rcPet = {0,0,0,0};
    GetWindowRect( g_target, &rcPet );

    printf( "pet window = %p rect=%ld,%ld %ldx%ld\n\n",
            (void*)g_target, rcPet.left, rcPet.top,
            rcPet.right - rcPet.left, rcPet.bottom - rcPet.top );

    // Walk the Z-order from front (top of the stack) to back.
    // GetTopWindow returns the topmost in the Z-order for the desktop's children.
    HWND hTop = GetTopWindow( NULL );
    int  nIndex = 0;
    int  nPetIndex = -1;
    int  nTopmostAbove = 0;
    int  nNonTopmostAbove = 0;

    printf( "--- Z-order from FRONT to BACK (first 40 windows) ---\n" );
    for( HWND h = hTop; h != NULL && nIndex < 2000; h = GetWindow( h, GW_HWNDNEXT ) )
    {
        if( !IsWindowVisible( h ) ) continue;

        RECT r = {0,0,0,0};
        GetWindowRect( h, &r );
        LONG_PTR ex = GetWindowLongPtrA( h, GWL_EXSTYLE );
        BOOL fTop = ( ex & WS_EX_TOPMOST ) ? TRUE : FALSE;

        DWORD dwPid = 0;
        GetWindowThreadProcessId( h, &dwPid );
        char szClass[128] = {0};
        GetClassNameA( h, szClass, sizeof szClass - 1 );

        // Only report windows that actually OVERLAP the pet, plus the pet itself.
        BOOL fOverlap = !( r.right <= rcPet.left || r.left >= rcPet.right ||
                           r.bottom <= rcPet.top || r.top >= rcPet.bottom );

        if( h == g_target )
        {
            nPetIndex = nIndex;
            printf( "  [%3d] >>> PET <<< %-30s topmost=%d pid=%lu\n",
                    nIndex, szClass, fTop, (unsigned long)dwPid );
        }
        else if( fOverlap )
        {
            printf( "  [%3d] %-34s topmost=%d rect=%ld,%ld %ldx%ld pid=%lu\n",
                    nIndex, szClass, fTop,
                    r.left, r.top, r.right - r.left, r.bottom - r.top,
                    (unsigned long)dwPid );
            if( nPetIndex < 0 )
            {
                if( fTop ) nTopmostAbove++;
                else       nNonTopmostAbove++;
            }
        }

        nIndex++;
        if( nPetIndex >= 0 && nIndex > nPetIndex + 3 ) break;
    }

    printf( "\n--- summary ---\n" );
    if( nPetIndex < 0 )
    {
        printf( "the pet was not found in the visible Z-order walk\n" );
        return 1;
    }
    printf( "windows above the pet that OVERLAP it: %d topmost, %d non-topmost\n",
            nTopmostAbove, nNonTopmostAbove );

    LONG_PTR exPet = GetWindowLongPtrA( g_target, GWL_EXSTYLE );
    printf( "pet exstyle TOPMOST bit = %d\n", ( exPet & WS_EX_TOPMOST ) ? 1 : 0 );

    if( nNonTopmostAbove > 0 )
        printf( "VERDICT: BROKEN — non-topmost windows sit ABOVE the pet.\n"
                "         Its WS_EX_TOPMOST intent has lapsed in the real Z-order,\n"
                "         so the cat is buried under normal windows.\n" );
    else if( ( exPet & WS_EX_TOPMOST ) && nTopmostAbove > 0 )
        printf( "VERDICT: WEAK — other topmost windows are above the pet\n"
                "         (a topmost window that keeps re-raising itself can bury it).\n" );
    else
        printf( "VERDICT: HEALTHY — nothing overlaps the pet from above.\n" );

    return 0;
}
