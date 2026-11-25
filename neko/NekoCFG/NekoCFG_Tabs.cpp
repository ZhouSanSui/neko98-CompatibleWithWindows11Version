/* property sheet functions */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "NekoCFG.h"
#include "..\NekoCommon.h"
#include "..\Hotkey.h"
#include "..\HotkeyList.h"
#include "..\Actions.h"
#include "NekoCFG_resource.h"
#include "NekoCFG_resource.hm"


/* help ID lookup table */
static DWORD dwHelpID[] = { 
    IDC_SCALESLIDER,    HIDC_SCALESLIDER,
    IDC_SCALEDISPLAY,   HIDC_SCALEDISPLAY,
    IDC_SET100,         HIDC_SET100,
    IDC_IMAGELIB,       HIDC_IMAGELIB,
    IDC_CHANGE,         HIDC_CHANGE,
    IDC_DEFAULT,        HIDC_DEFAULT,

    IDC_SPEEDSLIDER,    HIDC_SPEEDSLIDER,
    IDC_SENSESLIDER,    HIDC_SENSESLIDER,

    IDC_ACTION,         HIDC_ACTION,
    IDC_ACTIONDESC,     HIDC_ACTIONDESC,

	IDC_ALWAYSONTOP,	HIDC_ALWAYSONTOP,
    IDC_FOOTPRINTS,     HIDC_FOOTPRINTS,
    0,0
};


/**************************************************************************************/

//cat settings
LPCATSETTINGS g_lpCurrentCat = NULL;
#define MY_WRITESETTINGS (WM_USER+32)
#define MY_READSETTINGS  (WM_USER+33)

/**************************************************************************************/
//tab control data

#define NUM_PAGES 5
BOOL CALLBACK PropPage_Display( HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam );
BOOL CALLBACK PropPage_Movement( HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam );
BOOL CALLBACK PropPage_Hotkeys( HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam );
BOOL CALLBACK PropPage_Independence( HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam );
BOOL CALLBACK PropPage_Effects( HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam );
TABINITDATA tibTabs[NUM_PAGES] = {
    { "显示",        IDD_DISPLAY,        (DLGPROC)PropPage_Display,      IDI_TAB_DISPLAY,        NULL },
    { "移动",        IDD_MOVEMENT,       (DLGPROC)PropPage_Movement,     IDI_TAB_MOVEMENT,       NULL },
    { "快捷键",      IDD_HOTKEYS,        (DLGPROC)PropPage_Hotkeys,      IDI_TAB_HOTKEYS,        NULL },
    { "独立行为",    IDD_INDEPENDENCE,   (DLGPROC)PropPage_Independence, IDI_TAB_INDEPENDENCE,   NULL },
    { "特效",        IDD_EFFECTS,        (DLGPROC)PropPage_Effects,      IDI_TAB_EFFECTS,        NULL },
};

HWND g_hDlgDisplay = NULL;      //current child dialog
HWND g_hWndTab = NULL;          //tab control
RECT g_rcTabStart;              //offset in dialog to draw tab pages

HIMAGELIST g_himgTabIcons = NULL;

/**************************************************************************************/


/* InitialisePropertyDialog - loads all of the property sheet pages into the property sheet control */
void WINAPI InitialisePropertyDialog( HWND hWnd )
{
    int i;

    //create the image list
    g_himgTabIcons = ImageList_Create( 16, 16, ILC_COLOR4|ILC_MASK, NUM_PAGES, 0 );
    for( i = 0; i < NUM_PAGES; i++ )
    {
        HICON hIcon = (HICON)LoadImage( g_hInstance, MAKEINTRESOURCE(tibTabs[i].uIDIcon), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE );
        tibTabs[i].uIDIcon = ImageList_AddIcon( g_himgTabIcons, hIcon );
        DestroyIcon( hIcon );
    }
    TabCtrl_SetImageList( hWnd, g_himgTabIcons );


    //insert tabs for each of the dialog items and create the (hidden) dialog
    TC_ITEM tie;
    tie.mask = TCIF_TEXT | TCIF_IMAGE;
    for( i = 0; i < NUM_PAGES; i++ )
    {
        tie.iImage = tibTabs[i].uIDIcon;
        tie.pszText = tibTabs[i].szTitle;
        TabCtrl_InsertItem( hWnd, i, &tie );

        tibTabs[i].hDlg = CreateDialog( g_hInstance, MAKEINTRESOURCE(tibTabs[i].uIDDialog), GetParent(hWnd), tibTabs[i].lpfnDlgProc );
    }

    //calculate where to stick each dialog
    GetWindowRect( hWnd, &g_rcTabStart );
    ScreenToClient( GetParent(hWnd), (LPPOINT)&g_rcTabStart.left );
    ScreenToClient( GetParent(hWnd), (LPPOINT)&g_rcTabStart.right );
    g_rcTabStart.top += 42;
    g_rcTabStart.left += 3;

    //store the handle and show the first page
    g_hWndTab = hWnd;
    OnSelChanged( hWnd );
}

/* ShutdownPropertyDialog - releases any resources owned by the propery sheet */
void WINAPI ShutdownPropertyDialog()
{
    ImageList_Destroy( g_himgTabIcons );
}

/* OnTabSelection - updates the tabs when one is selected */
void WINAPI OnSelChanged( HWND hWnd ) 
{ 
    int iSel = TabCtrl_GetCurSel( hWnd ); 
 
    //destroy the current child dialog box, if any. 
    if( g_hDlgDisplay != NULL) ShowWindow( g_hDlgDisplay, SW_HIDE ); 
 
    //create the new child dialog box. 
    g_hDlgDisplay = tibTabs[iSel].hDlg;
    ShowWindow( g_hDlgDisplay, SW_SHOW );
    
}

/* SetActiveCat - selects which cat to configure */
void WINAPI SetActiveCat( LPCATSETTINGS lpCat )
{
    GetDialogSettings();
    g_lpCurrentCat = lpCat;
    for( int i = 0; i < NUM_PAGES; i++ ) SendMessage( tibTabs[i].hDlg, MY_READSETTINGS, 0, 0 );
}

/* SaveHotkeyPage - the hot keys are machine-wide, so their page must be
   flushed even when no cat is selected (GetDialogSettings does nothing in that
   case, which would silently drop edits made on the 快捷键 tab). */
void WINAPI SaveHotkeyPage()
{
    for( int i = 0; i < NUM_PAGES; i++ )
    {
        if( tibTabs[i].uIDDialog == (UINT)IDD_HOTKEYS )
        {
            if( tibTabs[i].hDlg ) SendMessage( tibTabs[i].hDlg, MY_WRITESETTINGS, 0, 0 );
            return;
        }
    }
}

/* GetDialogSettings - causes all property pages to write their settings into the data structure */
void WINAPI GetDialogSettings()
{
    if( g_lpCurrentCat ) 
        for( int i = 0; i < NUM_PAGES; i++ ) SendMessage( tibTabs[i].hDlg, MY_WRITESETTINGS, 0, 0 );
}

/**************************************************************************************/

/* PropPage_Display - message processsing function for the 'display' property page */
BOOL CALLBACK PropPage_Display( HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    switch( uMsg )
    {
        case WM_INITDIALOG:
            SendDlgItemMessage( hPage, IDC_SCALESLIDER, TBM_SETRANGE, FALSE, MAKELPARAM(MIN_SCALE/10,MAX_SCALE/10) );
            break;

        case MY_WRITESETTINGS:
            g_lpCurrentCat->uScale = SendDlgItemMessage( hPage, IDC_SCALESLIDER, TBM_GETPOS, 0, 0 ) * 10;
            GetDlgItemText( hPage, IDC_IMAGELIB, g_lpCurrentCat->szLibrary, MAX_PATH );
			g_lpCurrentCat->fAlwaysOnTop = IsDlgButtonChecked( hPage, IDC_ALWAYSONTOP );
            break;

        case MY_READSETTINGS:
            SendDlgItemMessage( hPage, IDC_SCALESLIDER, TBM_SETPOS, TRUE, g_lpCurrentCat->uScale / 10 );
            SetDlgItemText( hPage, IDC_IMAGELIB, g_lpCurrentCat->szLibrary );
			CheckDlgButton( hPage, IDC_ALWAYSONTOP, g_lpCurrentCat->fAlwaysOnTop );
            SendMessage( hPage, WM_HSCROLL, 0, 0 );
            break;

        case WM_HSCROLL:
        {
            //update the % indicator
            char szBuffer[32];
            sprintf( szBuffer, "%d%%", SendDlgItemMessage( hPage, IDC_SCALESLIDER, TBM_GETPOS, 0, 0 ) * 10 );
            SetDlgItemText( hPage, IDC_SCALEDISPLAY, szBuffer );
            break;
        }

        case WM_COMMAND:
        
            switch( LOWORD(wParam) )
            {
                case IDC_SET100:
                    SendDlgItemMessage( hPage, IDC_SCALESLIDER, TBM_SETPOS, TRUE, 10 );
                    SendMessage( hPage, WM_HSCROLL, 0, 0 );
                    break;

                case IDC_DEFAULT:
                    SetDlgItemText( hPage, IDC_IMAGELIB, "" );
                    break;

                case IDC_CHANGE:
                {
                    OPENFILENAME ofn;
                    char lpstrLibrary[MAX_PATH];
                    LPCTSTR lpstrFileMasks = "图标库\0*.icl;*.exe;*.dll\0所有文件 (*.*)\0*.*\0\0";

                    //get the current file
                    GetDlgItemText( hPage, IDC_IMAGELIB, lpstrLibrary, MAX_PATH-1 );
                    
                    ofn.lStructSize = sizeof(OPENFILENAME);
                    ofn.hwndOwner = hPage;
                    ofn.hInstance = NULL;
                    ofn.lpstrFilter = lpstrFileMasks;
                    ofn.lpstrCustomFilter = NULL;
                    ofn.nFilterIndex = 1;
                    ofn.lpstrFile = lpstrLibrary;
                    ofn.nMaxFile = MAX_PATH-1;
                    ofn.lpstrFileTitle = NULL;
                    ofn.nMaxFileTitle = 0;
                    ofn.lpstrInitialDir = NULL;
                    ofn.lpstrTitle = "选择图像库";
                    ofn.Flags = OFN_EXPLORER|OFN_HIDEREADONLY|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
                    ofn.lpstrDefExt = "icl";
                    if( GetOpenFileName( &ofn ) )
                    {
                        //check to make sure it's got enough icons in...
                        if( (UINT_PTR)ExtractIcon( g_hInstance, ofn.lpstrFile, (UINT)-1 ) < 32 )
                            MessageBox( hPage, "该文件中的图标数量不足——至少需要 32 个图标", "更改图像库", MB_ICONEXCLAMATION );
                        else
                            SetDlgItemText( hPage, IDC_IMAGELIB, ofn.lpstrFile );
                    }
                    break;
                }
            }
            break;

        case WM_SHOWWINDOW:
            if( wParam ) SetWindowPos( hPage, HWND_TOP, g_rcTabStart.left, g_rcTabStart.top, 0, 0, SWP_NOSIZE ); 
            break;

        /* help stuff */
        case WM_HELP:
            if( ((LPHELPINFO)lParam)->iCtrlId != (-1) )
                NekoHelp( (HWND)((LPHELPINFO)lParam)->hItemHandle, HELP_WM_HELP, (ULONG_PTR)dwHelpID );
            else
                return FALSE;
            break;

        case WM_CONTEXTMENU:
            NekoHelp( (HWND)wParam, HELP_CONTEXTMENU, (ULONG_PTR)dwHelpID );
            break;

        default:
            return FALSE;
    }
    return TRUE;
}


/* PropPage_Movement - message processsing function for the 'movement' property page */
BOOL CALLBACK PropPage_Movement( HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    switch( uMsg )
    {
        case WM_INITDIALOG:
            SendDlgItemMessage( hPage, IDC_SPEEDSLIDER, TBM_SETRANGE, FALSE, MAKELPARAM(MIN_SPEED,MAX_SPEED) );
            SendDlgItemMessage( hPage, IDC_SENSESLIDER, TBM_SETRANGE, FALSE, MAKELPARAM(MIN_SENSE,MAX_SENSE) );
            break;

        case MY_WRITESETTINGS:
            g_lpCurrentCat->uSpeed = SendDlgItemMessage( hPage, IDC_SPEEDSLIDER, TBM_GETPOS, 0, 0 );
            g_lpCurrentCat->uMouseSensitivity = MAX_SENSE - SendDlgItemMessage( hPage, IDC_SENSESLIDER, TBM_GETPOS, 0, 0 );
            break;

        case MY_READSETTINGS:
            SendDlgItemMessage( hPage, IDC_SPEEDSLIDER, TBM_SETPOS, TRUE, g_lpCurrentCat->uSpeed );
            SendDlgItemMessage( hPage, IDC_SENSESLIDER, TBM_SETPOS, TRUE, MAX_SENSE - g_lpCurrentCat->uMouseSensitivity );
            break;


        case WM_SHOWWINDOW:
            if( wParam ) SetWindowPos( hPage, HWND_TOP, g_rcTabStart.left, g_rcTabStart.top, 0, 0, SWP_NOSIZE ); 
            break;

        /* help stuff */
        case WM_HELP:
            if( ((LPHELPINFO)lParam)->iCtrlId != (-1) )
                NekoHelp( (HWND)((LPHELPINFO)lParam)->hItemHandle, HELP_WM_HELP, (ULONG_PTR)dwHelpID );
            else
                return FALSE;
            break;

        case WM_CONTEXTMENU:
            NekoHelp( (HWND)wParam, HELP_CONTEXTMENU, (ULONG_PTR)dwHelpID );
            break;

        default:
            return FALSE;
    }
    return TRUE;
}

/* PropPage_Hotkeys - the "快捷键" property page.

   This page owns the WHOLE shortcut list (it is a program-wide setting, not a
   per-cat one): one row per shortcut, each row carrying its own key combination
   and its own parameters.

   Layout, three bands:
     top    - "add a shortcut of this kind" (type combo + the '+' button)
     middle - the list, two columns (function / key)
     bottom - the editor for the selected row

   The bottom band is shared by every kind: the hot key control is always there,
   while the kind-specific fields (proxy address + port, browser + url, the two
   projection modes) occupy the same coordinates and exactly one group is shown
   at a time. Clicking the row that is already current collapses the detail band,
   which is what the user asked for ("再次点击方式名收缩"). */

static const char* g_szBrowserIds[] = { "edge", "chrome", "custom" };

#define HK_COL_FUNC 0
#define HK_COL_KEY  1

/* the row whose parameters are currently in the editor, and whether the
   detail band is expanded */
static int  s_nRow    = -1;
static BOOL s_bDetail = TRUE;

/* Guards against re-entrancy. ListView_SetItemState sends LVN_ITEMCHANGED,
   which lands back in this handler, which would select the row again: without
   this flag the handler recurses until the stack overflows (0xC00000FD, the
   "settings dialog vanishes after a few seconds" crash). */
static BOOL s_bInNotify = FALSE;

/* Pairs of (field, label) so the label follows its field. */
typedef struct tagHKFIELD
{
    UINT uField;
    UINT uLabel;
    int  nGroup;        /* 1 proxy, 2 website, 3 projection */
} HKFIELD;

static const HKFIELD g_aHkFields[] =
{
    { IDC_HKADDR,    IDC_HKADDRLBL,    1 },
    { IDC_HKPORT,    IDC_HKPORTLBL,    1 },
    { IDC_HKBROWSER, IDC_HKBROWSERLBL, 2 },
    { IDC_HKURL,     IDC_HKURLLBL,     2 },
    { IDC_HKBROWSE,  0,                2 },
    { IDC_HKPROJA,   IDC_HKPROJALBL,   3 },
    { IDC_HKPROJB,   IDC_HKPROJBLBL,   3 },
};
#define HK_FIELD_COUNT (int)( sizeof g_aHkFields / sizeof g_aHkFields[0] )

/* Which display group a kind of shortcut needs. 0 = none. */
static int HotkeyPageGroupOf( DWORD dwType )
{
    switch( dwType )
    {
        case NEKO_HT_PROXY:      return 1;
        case NEKO_HT_WEBSITE:    return 2;
        case NEKO_HT_PROJECTION: return 3;
        default:                 return 0;
    }
}

/* Show only the controls that belong to the given kind. When bVisible is FALSE
   the whole detail band is hidden, which is the collapsed state. */
static void HotkeyPageShowDetail( HWND hPage, DWORD dwType, BOOL bVisible )
{
    int  nGroup = bVisible ? HotkeyPageGroupOf( dwType ) : 0;
    int  i;

    for( i = 0; i < HK_FIELD_COUNT; i++ )
    {
        BOOL bShow = ( bVisible && g_aHkFields[i].nGroup == nGroup );

        //the browse button only matters for the "custom" browser, and that is
        //decided again in the CBN_SELCHANGE handler
        if( g_aHkFields[i].uField == IDC_HKBROWSE )
            bShow = bShow &&
                    ( SendDlgItemMessage( hPage, IDC_HKBROWSER, CB_GETCURSEL, 0, 0 ) == NEKO_BROWSER_CUSTOM );

        ShowWindow( GetDlgItem( hPage, g_aHkFields[i].uField ), bShow ? SW_SHOW : SW_HIDE );
        if( g_aHkFields[i].uLabel != 0 )
            ShowWindow( GetDlgItem( hPage, g_aHkFields[i].uLabel ), bShow ? SW_SHOW : SW_HIDE );
    }
}

/* Put the given row's values into the editor controls. */
static void HotkeyPageLoadRow( HWND hPage, int nRow )
{
    int nBrowser;

    if( nRow < 0 || nRow >= g_nHotkeyList )
    {
        SendDlgItemMessage( hPage, IDC_HKHOTKEY, HKM_SETHOTKEY, 0, 0 );
        SetDlgItemText( hPage, IDC_HKADDR, "" );
        SetDlgItemText( hPage, IDC_HKPORT, "" );
        SetDlgItemText( hPage, IDC_HKURL, "" );
        SetDlgItemText( hPage, IDC_HKINFO, "" );
        HotkeyPageShowDetail( hPage, NEKO_HT_HIDE, FALSE );
        return;
    }

    SendDlgItemMessage( hPage, IDC_HKHOTKEY, HKM_SETHOTKEY,
                        (WPARAM)(WORD)g_HotkeyList[nRow].dwHotkey, 0 );

    switch( g_HotkeyList[nRow].dwType )
    {
        case NEKO_HT_PROXY:
            SetDlgItemText( hPage, IDC_HKADDR, g_HotkeyList[nRow].szP1 );
            SetDlgItemText( hPage, IDC_HKPORT, g_HotkeyList[nRow].szP2 );
            SetDlgItemText( hPage, IDC_HKINFO, "按该快捷键可切换系统手动代理的开关。" );
            break;

        case NEKO_HT_WEBSITE:
            nBrowser = WebLauncherBrowserFromId( g_HotkeyList[nRow].szP2 );
            SendDlgItemMessage( hPage, IDC_HKBROWSER, CB_SETCURSEL, (WPARAM)nBrowser, 0 );
            SetDlgItemText( hPage, IDC_HKURL, g_HotkeyList[nRow].szP1 );
            SetDlgItemText( hPage, IDC_HKINFO, "按该快捷键可在指定浏览器里打开网址。" );
            break;

        case NEKO_HT_PROJECTION:
            SendDlgItemMessage( hPage, IDC_HKPROJA, CB_SETCURSEL,
                                (WPARAM)HotkeyProjectionIndex( g_HotkeyList[nRow].szP1 ), 0 );
            SendDlgItemMessage( hPage, IDC_HKPROJB, CB_SETCURSEL,
                                (WPARAM)HotkeyProjectionIndex( g_HotkeyList[nRow].szP2 ), 0 );
            SetDlgItemText( hPage, IDC_HKINFO, "按该快捷键在两种投影方式之间来回切换。" );
            break;

        case NEKO_HT_NOSLEEP:
            SetDlgItemText( hPage, IDC_HKINFO, "按该快捷键可阻止或恢复系统自动休眠。" );
            break;

        case NEKO_HT_HIDE:
        default:
            SetDlgItemText( hPage, IDC_HKINFO,
                            "按该快捷键依次：放大 300% -> 隐藏 -> 恢复正常显示。" );
            break;
    }

    HotkeyPageShowDetail( hPage, g_HotkeyList[nRow].dwType, s_bDetail );
}

/* Copy the editor controls back into the given row. Called before switching
   rows, before deleting, and before the list is written out. */
static void HotkeyPageStoreRow( HWND hPage, int nRow )
{
    WORD wKey;
    char szBuf[NEKO_HT_P1];

    if( nRow < 0 || nRow >= g_nHotkeyList ) return;

    wKey = (WORD)SendDlgItemMessage( hPage, IDC_HKHOTKEY, HKM_GETHOTKEY, 0, 0 );
    g_HotkeyList[nRow].dwHotkey = NekoHotkeyFromControl( wKey );

    switch( g_HotkeyList[nRow].dwType )
    {
        case NEKO_HT_PROXY:
            GetDlgItemText( hPage, IDC_HKADDR, szBuf, sizeof szBuf );
            lstrcpyn( g_HotkeyList[nRow].szP1, szBuf, NEKO_HT_P1 );
            GetDlgItemText( hPage, IDC_HKPORT, szBuf, sizeof szBuf );
            lstrcpyn( g_HotkeyList[nRow].szP2, szBuf, NEKO_HT_P2 );
            break;

        case NEKO_HT_WEBSITE:
        {
            int iSel = (int)SendDlgItemMessage( hPage, IDC_HKBROWSER, CB_GETCURSEL, 0, 0 );
            if( iSel < 0 || iSel > 2 ) iSel = NEKO_BROWSER_EDGE;
            lstrcpyn( g_HotkeyList[nRow].szP2, g_szBrowserIds[iSel], NEKO_HT_P2 );
            GetDlgItemText( hPage, IDC_HKURL, szBuf, sizeof szBuf );
            lstrcpyn( g_HotkeyList[nRow].szP1, szBuf, NEKO_HT_P1 );
            break;
        }

        case NEKO_HT_PROJECTION:
        {
            int iA = (int)SendDlgItemMessage( hPage, IDC_HKPROJA, CB_GETCURSEL, 0, 0 );
            int iB = (int)SendDlgItemMessage( hPage, IDC_HKPROJB, CB_GETCURSEL, 0, 0 );
            if( iA < 0 || iA > 3 ) iA = 2;
            if( iB < 0 || iB > 3 ) iB = 0;
            lstrcpyn( g_HotkeyList[nRow].szP1, HotkeyProjectionMode( iA ), NEKO_HT_P1 );
            lstrcpyn( g_HotkeyList[nRow].szP2, HotkeyProjectionMode( iB ), NEKO_HT_P2 );
            break;
        }

        default:
            break;
    }
}

/* Rebuild the two columns from the globals. */
static void HotkeyPageFillList( HWND hPage )
{
    HWND hList = GetDlgItem( hPage, IDC_HKLIST );
    int  i;

    SendMessage( hList, WM_SETREDRAW, FALSE, 0 );
    ListView_DeleteAllItems( hList );

    for( i = 0; i < g_nHotkeyList; i++ )
    {
        LVITEM lvi;
        char   szLabel[256];
        char   szKey[128];

        ZeroMemory( &lvi, sizeof lvi );
        lvi.mask     = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem    = i;
        lvi.iSubItem = 0;
        lvi.lParam   = (LPARAM)i;

        HotkeyListLabel( &g_HotkeyList[i], szLabel, sizeof szLabel );
        lvi.pszText = szLabel;
        ListView_InsertItem( hList, &lvi );

        NekoHotkeyFormat( g_HotkeyList[i].dwHotkey, szKey, sizeof szKey );
        ListView_SetItemText( hList, i, HK_COL_KEY, szKey );
    }

    SendMessage( hList, WM_SETREDRAW, TRUE, 0 );
    InvalidateRect( hList, NULL, TRUE );
}

/* Select one row and load it into the editor. */
static void HotkeyPageSelect( HWND hPage, int nRow )
{
    HWND hList = GetDlgItem( hPage, IDC_HKLIST );
    BOOL bWasInNotify = s_bInNotify;

    /* Selecting a row sends LVN_ITEMCHANGED straight back into the page. The
       caller is usually reacting to that very notification, so swallow it:
       otherwise selecting would re-enter the handler and toggle the detail
       band (or, before the guard existed, recurse until the stack blew). */
    s_bInNotify = TRUE;

    if( nRow < 0 || nRow >= g_nHotkeyList ) nRow = -1;
    s_nRow = nRow;

    ListView_SetItemState( hList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED );
    if( nRow >= 0 )
    {
        ListView_SetItemState( hList, nRow, LVIS_SELECTED | LVIS_FOCUSED,
                               LVIS_SELECTED | LVIS_FOCUSED );
        ListView_EnsureVisible( hList, nRow, FALSE );
    }
    HotkeyPageLoadRow( hPage, nRow );
    s_bInNotify = bWasInNotify;
}

/* PropPage_Hotkeys - message processsing function for the 'hot keys' page */
BOOL CALLBACK PropPage_Hotkeys( HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    switch( uMsg )
    {
        case WM_INITDIALOG:
        {
            HWND hList = GetDlgItem( hPage, IDC_HKLIST );
            LVCOLUMN col;
            int i;

            ZeroMemory( &col, sizeof col );
            col.mask     = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText  = "快捷方式功能";
            col.cx       = 92;
            col.iSubItem = HK_COL_FUNC;
            ListView_InsertColumn( hList, HK_COL_FUNC, &col );
            col.pszText  = "快捷键";
            col.cx       = 45;
            col.iSubItem = HK_COL_KEY;
            ListView_InsertColumn( hList, HK_COL_KEY, &col );
            ListView_SetExtendedListViewStyle( hList, LVS_EX_FULLROWSELECT );

            for( i = 0; i < NEKO_HT_TYPES; i++ )
                SendDlgItemMessage( hPage, IDC_HKTYPE, CB_ADDSTRING, 0,
                                    (LPARAM)HotkeyTypeName( (DWORD)i ) );
            SendDlgItemMessage( hPage, IDC_HKTYPE, CB_SETCURSEL, 0, 0 );

            for( i = 0; i < 3; i++ )
                SendDlgItemMessage( hPage, IDC_HKBROWSER, CB_ADDSTRING, 0,
                                    (LPARAM)WebLauncherBrowserName( i ) );
            SendDlgItemMessage( hPage, IDC_HKBROWSER, CB_SETCURSEL, 0, 0 );

            for( i = 0; i < NEKO_PROJECTION_MODES; i++ )
            {
                SendDlgItemMessage( hPage, IDC_HKPROJA, CB_ADDSTRING, 0,
                                    (LPARAM)HotkeyProjectionMode( i ) );
                SendDlgItemMessage( hPage, IDC_HKPROJB, CB_ADDSTRING, 0,
                                    (LPARAM)HotkeyProjectionMode( i ) );
            }
            SendDlgItemMessage( hPage, IDC_HKPROJA, CB_SETCURSEL, 2, 0 );
            SendDlgItemMessage( hPage, IDC_HKPROJB, CB_SETCURSEL, 0, 0 );

            //a combination without a modifier would be stolen from every other
            //program, so one is required
            SendDlgItemMessage( hPage, IDC_HKHOTKEY, HKM_SETRULES,
                                HKCOMB_NONE | HKCOMB_S | HKCOMB_C | HKCOMB_A |
                                HKCOMB_SC | HKCOMB_SA | HKCOMB_CA,
                                MAKELPARAM( HOTKEYF_CONTROL | HOTKEYF_ALT, 0 ) );

            HotkeyListLoad();
            HotkeyPageFillList( hPage );
            HotkeyPageSelect( hPage, g_nHotkeyList > 0 ? 0 : -1 );
            break;
        }

        case MY_WRITESETTINGS:
            HotkeyPageStoreRow( hPage, s_nRow );
            HotkeyListSave();
            break;

        case MY_READSETTINGS:
            HotkeyListLoad();
            HotkeyPageFillList( hPage );
            HotkeyPageSelect( hPage, ( s_nRow >= 0 && s_nRow < g_nHotkeyList ) ? s_nRow
                                                                              : ( g_nHotkeyList > 0 ? 0 : -1 ) );
            break;

        case WM_NOTIFY:
        {
            NMHDR* pHdr = (NMHDR*)lParam;

            if( pHdr->idFrom == (UINT)IDC_HKLIST && pHdr->code == LVN_ITEMCHANGED
                && !s_bInNotify )
            {
                NMLISTVIEW* pLV = (NMLISTVIEW*)lParam;

                //only react to a row BECOMING selected
                if( ( pLV->uChanged & LVIF_STATE ) &&
                    ( pLV->uNewState & LVIS_SELECTED ) &&
                    !( pLV->uOldState & LVIS_SELECTED ) )
                {
                    int nRow = pLV->iItem;

                    s_bInNotify = TRUE;

                    HotkeyPageStoreRow( hPage, s_nRow );

                    //moving to a different row always shows its settings; the
                    //collapse toggle lives in the NM_CLICK branch below
                    s_bDetail = TRUE;

                    HotkeyPageSelect( hPage, nRow );
                    s_bInNotify = FALSE;
                }
            }
            else if( pHdr->idFrom == (UINT)IDC_HKLIST && pHdr->code == NM_CLICK )
            {
                /* Clicking the row that is ALREADY current collapses the
                   detail band and clicking it again expands it -- that is the
                   "再次点击方式名收缩" behaviour the user asked for. A click on a
                   different row arrives as LVN_ITEMCHANGED first, so by the time
                   we get here s_nRow is already that row and nothing toggles. */
                HWND hList = GetDlgItem( hPage, IDC_HKLIST );
                LVHITTESTINFO ht;

                ZeroMemory( &ht, sizeof ht );
                GetCursorPos( &ht.pt );
                ScreenToClient( hList, &ht.pt );

                if( ListView_HitTest( hList, &ht ) == s_nRow && s_nRow >= 0 )
                {
                    s_bDetail = !s_bDetail;
                    HotkeyPageLoadRow( hPage, s_nRow );
                }
            }
            break;
        }

        case WM_COMMAND:
            switch( LOWORD(wParam) )
            {
                case IDC_HKADD:
                {
                    int iType = (int)SendDlgItemMessage( hPage, IDC_HKTYPE, CB_GETCURSEL, 0, 0 );
                    int nNew;

                    if( iType < 0 || iType >= NEKO_HT_TYPES ) break;

                    HotkeyPageStoreRow( hPage, s_nRow );

                    nNew = HotkeyListAppend( (DWORD)iType );
                    if( nNew < 0 )
                    {
                        MessageBox( hPage, "快捷方式数量已达上限（24 个）。",
                                    szNekoWindowTitle, MB_ICONINFORMATION );
                        break;
                    }
                    HotkeyListSave();
                    HotkeyPageFillList( hPage );
                    s_bDetail = TRUE;
                    HotkeyPageSelect( hPage, nNew );
                    //the first thing the user wants with a new row is a key
                    SetFocus( GetDlgItem( hPage, IDC_HKHOTKEY ) );
                    break;
                }

                case IDC_HKDEL:
                {
                    if( s_nRow < 0 || s_nRow >= g_nHotkeyList )
                    {
                        MessageBox( hPage, "请先在列表中选择要删除的快捷方式。",
                                    szNekoWindowTitle, MB_ICONINFORMATION );
                        break;
                    }
                    if( MessageBox( hPage, "确定要删除选中的快捷方式吗？",
                                    szNekoWindowTitle, MB_YESNO | MB_ICONQUESTION ) != IDYES )
                        break;

                    HotkeyListRemove( s_nRow );
                    HotkeyListSave();

                    if( s_nRow >= g_nHotkeyList ) s_nRow = g_nHotkeyList - 1;
                    HotkeyPageFillList( hPage );
                    HotkeyPageSelect( hPage, g_nHotkeyList > 0 ? s_nRow : -1 );
                    break;
                }

                case IDC_HKBROWSE:
                {
                    OPENFILENAME ofn;
                    char szPath[MAX_PATH] = "";

                    ZeroMemory( &ofn, sizeof ofn );
                    ofn.lStructSize = sizeof ofn;
                    ofn.hwndOwner   = hPage;
                    ofn.lpstrFilter = "可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0\0";
                    ofn.lpstrFile   = szPath;
                    ofn.nMaxFile    = MAX_PATH - 1;
                    ofn.lpstrTitle  = "选择浏览器程序";
                    ofn.Flags       = OFN_EXPLORER | OFN_HIDEREADONLY |
                                      OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    ofn.lpstrDefExt = "exe";

                    if( GetOpenFileName( &ofn ) )
                        SetDlgItemText( hPage, IDC_HKURL, szPath );
                    break;
                }

                case IDC_HKBROWSER:
                    if( HIWORD( wParam ) == CBN_SELCHANGE )
                    {
                        int iSel = (int)SendDlgItemMessage( hPage, IDC_HKBROWSER, CB_GETCURSEL, 0, 0 );
                        ShowWindow( GetDlgItem( hPage, IDC_HKBROWSE ),
                                    ( s_bDetail && iSel == NEKO_BROWSER_CUSTOM ) ? SW_SHOW : SW_HIDE );
                    }
                    break;
            }
            break;

        case WM_SHOWWINDOW:
            if( wParam ) SetWindowPos( hPage, HWND_TOP, g_rcTabStart.left, g_rcTabStart.top, 0, 0, SWP_NOSIZE );
            break;

        /* help stuff */
        case WM_HELP:
            if( ((LPHELPINFO)lParam)->iCtrlId != (-1) )
                NekoHelp( (HWND)((LPHELPINFO)lParam)->hItemHandle, HELP_WM_HELP, (ULONG_PTR)dwHelpID );
            else
                return FALSE;
            break;

        case WM_CONTEXTMENU:
            NekoHelp( (HWND)wParam, HELP_CONTEXTMENU, (ULONG_PTR)dwHelpID );
            break;

        default:
            return FALSE;
    }
    return TRUE;
}


/* PropPage_Independence - message processsing function for the 'independence' property page */
BOOL CALLBACK PropPage_Independence( HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    switch( uMsg )
    {
        case WM_INITDIALOG:
            SendDlgItemMessage( hPage, IDC_ACTION, CB_ADDSTRING, 0, (LPARAM)"追逐鼠标（默认）" );
            SendDlgItemMessage( hPage, IDC_ACTION, CB_ADDSTRING, 0, (LPARAM)"远离鼠标");
            SendDlgItemMessage( hPage, IDC_ACTION, CB_ADDSTRING, 0, (LPARAM)"随机乱跑");
            SendDlgItemMessage( hPage, IDC_ACTION, CB_ADDSTRING, 0, (LPARAM)"沿屏幕边缘踱步");
            SendDlgItemMessage( hPage, IDC_ACTION, CB_ADDSTRING, 0, (LPARAM)"在屏幕上奔跑");
            break;

        case MY_WRITESETTINGS:
            g_lpCurrentCat->uAction = SendDlgItemMessage( hPage, IDC_ACTION, CB_GETCURSEL, 0, 0 );
            break;

        case MY_READSETTINGS:
            SendDlgItemMessage( hPage, IDC_ACTION, CB_SETCURSEL, g_lpCurrentCat->uAction, 0 );
            SendMessage( hPage, WM_COMMAND, MAKEWPARAM(IDC_ACTION, CBN_SELCHANGE), 0 );
            break;

        case WM_SHOWWINDOW:
            if( wParam ) SetWindowPos( hPage, HWND_TOP, g_rcTabStart.left, g_rcTabStart.top, 0, 0, SWP_NOSIZE ); 
            break;

        case WM_COMMAND:
            switch( LOWORD(wParam) )
            {
                case IDC_ACTION:
                    if( HIWORD(wParam) == CBN_SELCHANGE )
                    {
                        const char* szActionDesc[] = {
                            "小猫会跟随鼠标光标，走到哪里跟到哪里",
                            "当鼠标靠近时小猫会逃跑，远离时会睡觉",
                            "小猫会跑到桌面上的随机位置，睡一会儿，然后继续乱跑",
                            "小猫会沿着桌面的边缘奔跑",
                            "小猫会在桌面上不停地奔跑"
                        };

                        SetDlgItemText( hPage, IDC_ACTIONDESC, szActionDesc[SendDlgItemMessage( hPage, IDC_ACTION, CB_GETCURSEL, 0, 0 )] );
                    }
                    break;
            }
            break;

        /* help stuff */
        case WM_HELP:
            if( ((LPHELPINFO)lParam)->iCtrlId != (-1) )
                NekoHelp( (HWND)((LPHELPINFO)lParam)->hItemHandle, HELP_WM_HELP, (ULONG_PTR)dwHelpID );
            else
                return FALSE;
            break;

        case WM_CONTEXTMENU:
            NekoHelp( (HWND)wParam, HELP_CONTEXTMENU, (ULONG_PTR)dwHelpID );
            break;

        default:
            return FALSE;
    }
    return TRUE;
}




/* PropPage_Effects - message processsing function for the 'effects' property page */
BOOL CALLBACK PropPage_Effects( HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    switch( uMsg )
    {
        case MY_WRITESETTINGS:
            GetDlgItemText( hPage, IDC_IMAGELIB, g_lpCurrentCat->szFootprintLib, MAX_PATH );
			g_lpCurrentCat->bFootprints = IsDlgButtonChecked( hPage, IDC_FOOTPRINTS );
            break;

        case MY_READSETTINGS:
            SetDlgItemText( hPage, IDC_IMAGELIB, g_lpCurrentCat->szFootprintLib );
			CheckDlgButton( hPage, IDC_FOOTPRINTS, g_lpCurrentCat->bFootprints );
            break;

        case WM_COMMAND:
        
            switch( LOWORD(wParam) )
            {
                case IDC_DEFAULT:
                    SetDlgItemText( hPage, IDC_IMAGELIB, "" );
                    break;

                case IDC_CHANGE:
                {
                    OPENFILENAME ofn;
                    char lpstrLibrary[MAX_PATH];
                    LPCTSTR lpstrFileMasks = "图标库\0*.icl;*.exe;*.dll\0所有文件 (*.*)\0*.*\0\0";

                    //get the current file
                    GetDlgItemText( hPage, IDC_IMAGELIB, lpstrLibrary, MAX_PATH-1 );
                    
                    ofn.lStructSize = sizeof(OPENFILENAME);
                    ofn.hwndOwner = hPage;
                    ofn.hInstance = NULL;
                    ofn.lpstrFilter = lpstrFileMasks;
                    ofn.lpstrCustomFilter = NULL;
                    ofn.nFilterIndex = 1;
                    ofn.lpstrFile = lpstrLibrary;
                    ofn.nMaxFile = MAX_PATH-1;
                    ofn.lpstrFileTitle = NULL;
                    ofn.nMaxFileTitle = 0;
                    ofn.lpstrInitialDir = NULL;
                    ofn.lpstrTitle = "选择脚印图像库";
                    ofn.Flags = OFN_EXPLORER|OFN_HIDEREADONLY|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
                    ofn.lpstrDefExt = "icl";
                    if( GetOpenFileName( &ofn ) )
                    {
                        //check to make sure it's got enough icons in...
                        if( (UINT_PTR)ExtractIcon( g_hInstance, ofn.lpstrFile, (UINT)-1 ) < 8 )
                            MessageBox( hPage, "该文件中的图标数量不足——至少需要 8 个图标", "更改图像库", MB_ICONEXCLAMATION );
                        else
                        {
                            SetDlgItemText( hPage, IDC_IMAGELIB, ofn.lpstrFile );
			                CheckDlgButton( hPage, IDC_FOOTPRINTS, TRUE );
                        }
                    }
                    break;
                }
            }
            break;

        case WM_SHOWWINDOW:
            if( wParam ) SetWindowPos( hPage, HWND_TOP, g_rcTabStart.left, g_rcTabStart.top, 0, 0, SWP_NOSIZE ); 
            break;

        /* help stuff */
        case WM_HELP:
            if( ((LPHELPINFO)lParam)->iCtrlId != (-1) )
                NekoHelp( (HWND)((LPHELPINFO)lParam)->hItemHandle, HELP_WM_HELP, (ULONG_PTR)dwHelpID );
            else
                return FALSE;
            break;

        case WM_CONTEXTMENU:
            NekoHelp( (HWND)wParam, HELP_CONTEXTMENU, (ULONG_PTR)dwHelpID );
            break;

        default:
            return FALSE;
    }
    return TRUE;
}
