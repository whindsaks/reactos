#pragma once

#include <atlstr.h>

#ifdef _M_IX86
#define CurrentArchitecture L"x86"
#elif defined(_M_AMD64)
#define CurrentArchitecture L"amd64"
#elif defined(_M_ARM)
#define CurrentArchitecture L"arm"
#elif defined(_M_ARM64)
#define CurrentArchitecture L"arm64"
#elif defined(_M_IA64)
#define CurrentArchitecture L"ia64"
#elif defined(_M_PPC)
#define CurrentArchitecture L"ppc"
#endif

#if defined(_M_IX86) || defined(_M_AMD64)
#ifdef _MSC_VER
#include <intrin.h>
#endif
struct CPUID
{
    UINT reg[4];
    CPUID() {}
    CPUID(UINT Eax) { Call(Eax); }
    CPUID& Call(UINT Eax)
    {
#ifdef _MSC_VER
        __cpuid((int*)reg, Eax);
#else
        asm volatile ("cpuid" : "=a" (reg[0]), "=b" (reg[1]), "=c" (reg[2]), "=d" (reg[3]) : "a" (Eax), "c" (0));
#endif
        return *this;
    }
    UINT EAX() const { return reg[0]; }
    UINT EBX() const { return reg[1]; }
    UINT ECX() const { return reg[2]; }
    UINT EDX() const { return reg[3]; }
};
#endif

static inline UINT
ErrorFromHResult(HRESULT hr)
{
    // Attempt to extract the original Win32 error code from the HRESULT
    if (HIWORD(hr) == HIWORD(HRESULT_FROM_WIN32(!0)))
        return LOWORD(hr);
    else
        return hr >= 0 ? ERROR_SUCCESS : hr;
}

VOID
CopyTextToClipboard(LPCWSTR lpszText);
VOID
ShowPopupMenuEx(HWND hwnd, HWND hwndOwner, UINT MenuID, UINT DefaultItem);
BOOL
StartProcess(const CStringW &Path, BOOL Wait);
BOOL
GetStorageDirectory(CStringW &lpDirectory);

VOID
InitLogs();
VOID
FreeLogs();
BOOL
WriteLogMessage(WORD wType, DWORD dwEventID, LPCWSTR lpMsg);
BOOL
GetInstalledVersion(CStringW *pszVersion, const CStringW &szRegName);

typedef struct
{
    const CStringW &ItemPath;
    UINT64 UncompressedSize;
    UINT FileAttributes;
} EXTRACTCALLBACKINFO;
typedef BOOL (CALLBACK*EXTRACTCALLBACK)(const EXTRACTCALLBACKINFO &Info, void *Cookie);

static inline BOOL
NotifyFileExtractCallback(const CStringW &ItemPath, UINT64 UncompressedSize, UINT FileAttributes,
                          EXTRACTCALLBACK Callback, void *Cookie)
{
    EXTRACTCALLBACKINFO eci = { ItemPath, UncompressedSize, FileAttributes };
    return Callback ? Callback(eci, Cookie) : TRUE;
}

BOOL
ExtractFilesFromCab(const CStringW &szCabName, const CStringW &szCabDir, const CStringW &szOutputDir,
                    EXTRACTCALLBACK Callback = NULL, void *Cookie = NULL);
BOOL
ExtractFilesFromCab(LPCWSTR FullCabPath, const CStringW &szOutputDir,
                    EXTRACTCALLBACK Callback = NULL, void *Cookie = NULL);

BOOL
IsSystem64Bit();

INT
GetSystemColorDepth();

void
UnixTimeToFileTime(DWORD dwUnixTime, LPFILETIME pFileTime);

BOOL
SearchPatternMatch(LPCWSTR szHaystack, LPCWSTR szNeedle);

HRESULT
RegKeyHasValues(HKEY hKey, LPCWSTR Path, REGSAM wowsam = 0);
LPCWSTR
GetRegString(CRegKey &Key, LPCWSTR Name, CStringW &Value);

bool
ExpandEnvStrings(CStringW &Str);

template <class T> static CStringW
BuildPath(const T &Base, LPCWSTR Append)
{
    CStringW path = Base;
    SIZE_T len = path.GetLength();
    if (len && path[len - 1] != L'\\' && path[len - 1] != L'/')
        path += L'\\';
    while (*Append == L'\\' || *Append == L'/')
        ++Append;
    return path + Append;
}

CStringW
SplitFileAndDirectory(LPCWSTR FullPath, CStringW *pDir = NULL);
BOOL
DeleteDirectoryTree(LPCWSTR Dir, HWND hwnd = NULL);
UINT
CreateDirectoryTree(LPCWSTR Dir);
HRESULT
GetSpecialPath(UINT csidl, CStringW &Path, HWND hwnd = NULL);
HRESULT
GetKnownPath(REFKNOWNFOLDERID kfid, CStringW &Path, DWORD Flags = KF_FLAG_CREATE);
HRESULT
GetProgramFilesPath(CStringW &Path, BOOL PerUser, HWND hwnd = NULL);

template <class T> class CLocalPtr : public CHeapPtr<T, CLocalAllocator>
{
};
