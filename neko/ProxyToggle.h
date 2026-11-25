///////////////////////////////////////////////////////////////////////////////
// ProxyToggle.h
//
// Turns the Windows manual proxy on and off.
//
// The settings live in the per-user Internet Settings key, which is what the
// "Proxy" settings page in Windows itself edits:
//
//   HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings
//       ProxyEnable    REG_DWORD   0 = off, 1 = on
//       ProxyServer    REG_SZ      "host:port"
//       ProxyOverride  REG_SZ      ";"-separated bypass list; "<local>" is the
//                                  "do not use the proxy for local addresses"
//                                  checkbox
//
// Writing the registry alone is NOT enough: the WinINET cache keeps the old
// settings until it is told to re-read them. InternetSetOption with
// INTERNET_OPTION_SETTINGS_CHANGED + INTERNET_OPTION_REFRESH is what makes the
// change take effect without a logout, and it also prompts running
// applications (Chrome/Edge/Office) to pick the new configuration up.
///////////////////////////////////////////////////////////////////////////////

#ifndef __PROXYTOGGLE_H__
#define __PROXYTOGGLE_H__

//The proxy used when the caller does not name one. Each shortcut entry can carry
//its own host and port (see HotkeyList.h), so the address is a parameter now;
//this default only keeps older call sites - and a registry that has never been
//edited - working.
#define NEKO_PROXY_SERVER    "192.168.10.210:7890"

//The bypass list the user asked for, plus "<local>" which is Windows' own
//token for "do not use the proxy server for local addresses".
#define NEKO_PROXY_BYPASS \
    "*zhihu.com;*zhimg.com;*jd.com;100ime-iat-api.xfyun.cn;*360buyimg.com;" \
    "localhost;*.local;127.*;10.*;172.16.*;172.17.*;172.18.*;172.19.*;" \
    "172.2*;172.30.*;172.31.*;192.168.*;*.zhousansui.top;<local>"

//TRUE when the Windows manual proxy is currently switched on.
BOOL ProxyIsEnabled();

//Switch the manual proxy on or off.
//
//Enabling also (re)writes ProxyServer and ProxyOverride, so a machine whose
//proxy is still blank ends up with a usable configuration. Disabling only clears
//ProxyEnable and leaves the address in place, so the next enable is instant and
//the user does not lose a hand-edited address.
//
//szServer is "host:port". Pass NULL (or an empty string) to keep the built-in
//default, which is what older call sites do.
//
//Returns TRUE when the registry write and the WinINET refresh both succeeded.
BOOL ProxySetEnabled( BOOL bEnable );
BOOL ProxySetEnabledEx( BOOL bEnable, const char* szServer );

//Convenience: flip the current state and report the state AFTER the flip.
BOOL ProxyToggle();
BOOL ProxyToggleEx( const char* szServer );

//Read back the address currently configured, as "host:port". Used by the
//settings dialog to show what is really in force and by the migration path.
BOOL ProxyGetServer( char* pszOut, int cbOut );

#endif //__PROXYTOGGLE_H__
