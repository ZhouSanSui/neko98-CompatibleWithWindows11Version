/************************************
 
    Neko's configuration

 *************************************/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include "NekoSettings.h"

CNekoSettings::CNekoSettings( LPCSTR key, BOOL fCreate /*TRUE*/ )
{
	m_hKey = NULL;
	if( fCreate )
	{
		DWORD dwDisposition;
		RegCreateKeyEx( HKEY_CURRENT_USER, key, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ|KEY_WRITE, NULL, &m_hKey, &dwDisposition );
	}
	else
	{
		if( RegOpenKeyEx( HKEY_CURRENT_USER, key, 0, KEY_READ|KEY_WRITE, &m_hKey ) != ERROR_SUCCESS )
			m_hKey = NULL;
	}
}

CNekoSettings::~CNekoSettings()
{
    if( m_hKey ) RegCloseKey( m_hKey );
}

BOOL CNekoSettings::IsOpen()
{
	return( m_hKey != NULL );
}

BOOL CNekoSettings::GetString( LPCSTR keyname, LPSTR result, ULONG size )
{
    //can't write anywhere sensible
    if( result == NULL || size == 0 )
        return FALSE;

    if( m_hKey == NULL )
    {
        result[0] = '\0';
        return FALSE;
    }

    DWORD dwType = 0;
    DWORD dwSize = (DWORD)size;   //honour the caller's buffer size

    //zero the caller's buffer up front so a failure never leaves garbage behind
    memset( result, 0, size );

    if( RegQueryValueEx( m_hKey, keyname, NULL, &dwType, (LPBYTE)result, &dwSize ) != ERROR_SUCCESS )
    {
        result[0] = '\0';
        return FALSE;
    }

    //only string types are acceptable
    if( dwType != REG_SZ && dwType != REG_EXPAND_SZ )
    {
        memset( result, 0, size );
        return FALSE;
    }

    //always NUL-terminate, even when the value was truncated to fit
    result[size - 1] = '\0';

    return TRUE;
}

BOOL CNekoSettings::GetInt( LPCSTR keyname, DWORD * result )
{
    if( result == NULL )
        return FALSE;

    //clear the out value so a failure can never leak uninitialised data
    *result = 0;

    if( m_hKey == NULL )
        return FALSE;

    DWORD dwType = 0;
    DWORD dwValue = 0;
    DWORD dwSize = sizeof(DWORD);

    if( RegQueryValueEx( m_hKey, keyname, NULL, &dwType, (LPBYTE)&dwValue, &dwSize ) != ERROR_SUCCESS )
        return FALSE;

    //accept a DWORD, or a 4-byte REG_BINARY (legacy representation)
    if( dwType == REG_DWORD )
    {
        *result = dwValue;
        return TRUE;
    }

    if( dwType == REG_BINARY && dwSize == sizeof(DWORD) )
    {
        *result = dwValue;
        return TRUE;
    }

    return FALSE;
}

BOOL CNekoSettings::GetBool( LPCSTR keyname, BOOL * result )
{
    if( result == NULL )
        return FALSE;

    //clear the out value so a failure can never leak uninitialised data
    *result = FALSE;

    if( m_hKey == NULL )
        return FALSE;

    DWORD dwType = 0;
    BYTE  byBuffer[8];
    DWORD dwSize = sizeof(byBuffer);

    memset( byBuffer, 0, sizeof(byBuffer) );

    if( RegQueryValueEx( m_hKey, keyname, NULL, &dwType, (LPBYTE)byBuffer, &dwSize ) != ERROR_SUCCESS )
        return FALSE;

    //REG_DWORD, or a legacy 4-byte REG_BINARY DWORD
    if( dwType == REG_DWORD || ( dwType == REG_BINARY && dwSize == sizeof(DWORD) ) )
    {
        *result = ( *((DWORD*)byBuffer) != 0 ) ? TRUE : FALSE;
        return TRUE;
    }

    //be tolerant of a string representation as well
    if( dwType == REG_SZ || dwType == REG_EXPAND_SZ )
    {
        char sz[sizeof(byBuffer) + 1];
        memcpy( sz, byBuffer, sizeof(byBuffer) );
        sz[sizeof(byBuffer)] = '\0';

        *result = ( atoi( sz ) != 0 ) ? TRUE : FALSE;
        return TRUE;
    }

    return FALSE;
}


BOOL CNekoSettings::SetString( LPCSTR keyname, LPSTR value )
{
    ULONG ulSize = strlen( value ) + 1;
    DWORD dwZero = 0;
    return ( RegSetValueEx( m_hKey, keyname, dwZero, REG_SZ, (LPBYTE)value, ulSize) == ERROR_SUCCESS );
}

BOOL CNekoSettings::SetInt( LPCSTR keyname, DWORD value )
{
    ULONG ulSize = sizeof(DWORD);
    DWORD dwZero = 0;
    return ( RegSetValueEx( m_hKey, keyname, dwZero, REG_DWORD, (LPBYTE)&value, ulSize ) == ERROR_SUCCESS );
}

BOOL CNekoSettings::SetBool( LPCSTR keyname, BOOL value )
{
    //write a REG_DWORD going forward; GetBool still understands the legacy
    //REG_BINARY form so existing users do not lose their setting
    DWORD dwValue = value ? 1 : 0;
    ULONG ulSize = sizeof(DWORD);
    DWORD dwZero = 0;
    return ( RegSetValueEx( m_hKey, keyname, dwZero, REG_DWORD, (LPBYTE)&dwValue, ulSize ) == ERROR_SUCCESS );
}
