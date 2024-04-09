#pragma once
#define COBJMACROS
#define NTOS_MODE_USER
#include <windows.h>
#include <atlbase.h>
#include <atlcom.h>
#include <strsafe.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlguid_undoc.h>
#define NTSTATUS LONG // for debug.h
#include <reactos/debug.h>
#include <shellutils.h>
#include <ntquery.h>
#include <fdi.h>
#include "resource.h"

#ifndef DIAMONDAPI
#define DIAMONDAPI __cdecl
#endif
#ifndef SFGAO_SYSTEM
#define SFGAO_SYSTEM 0x00001000
#endif
#ifndef TB_GETPRESSEDIMAGELIST
#define TB_GETPRESSEDIMAGELIST (WM_USER + 105)
#endif

EXTERN_C INT WINAPI SHFormatDateTimeA(const FILETIME UNALIGNED *fileTime, DWORD *flags, LPSTR buf, UINT size);

template<class T> static inline bool IsPathSep(T c) { return c == '\\' || c == '/'; }

enum EXTRACTCALLBACKMSG { ECM_BEGIN, ECM_FILE, ECM_PREPAREPATH, ECM_ERROR };
struct EXTRACTCALLBACKDATA
{
    LPCWSTR Path;
    const FDINOTIFICATION *pfdin;
    HRESULT hr;
};
typedef HRESULT (CALLBACK*EXTRACTCALLBACK)(EXTRACTCALLBACKMSG msg, const EXTRACTCALLBACKDATA &data, LPVOID cookie);
HRESULT ExtractCabinet(LPCWSTR cab, LPCWSTR destination, EXTRACTCALLBACK callback, LPVOID cookie);
