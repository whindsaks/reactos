/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Test delayloaded functions
 * COPYRIGHT:   Copyright 2026 Whindmar Saksit <whindsaks@proton.me>
 */

#include <apitest.h>
#include <windows.h>

#define ENVNAME "ROS_TEST_DL_PARAM"
#define TESTSUCCESSCODE 0x04200000 // Must not be STILL_ACTIVE
#define Finish(success) ((*g_szModule) ? (ExitProcess( (success) ? TESTSUCCESSCODE : 0xDEAD ), 0) : 0 )

CHAR g_szModule[MAX_PATH];

static BOOL RunTest(PCSTR pszModule)
{
    DWORD ec;
    PROCESS_INFORMATION pi;
    STARTUPINFOW si = { sizeof(si) };
    WCHAR cmdline[MAX_PATH + 42];
    DWORD flags = DETACHED_PROCESS | CREATE_NO_WINDOW;

    if (*g_szModule)
    {
        // We are a child process running a test but is it for the requested module?
        if (lstrcmpiA(g_szModule, pszModule))
        {
            return FALSE;
        }
        if (!LoadLibraryA(pszModule))
        {
            skip("Unable to load %s\n", pszModule);
            return FALSE;
        }
        return TRUE;
    }

    if (!SetEnvironmentVariableA(ENVNAME, pszModule))
    {
        skip("Unable to initialize test for %s\n", pszModule);
        return FALSE;
    }
    lstrcatW(cmdline + GetModuleFileNameW(NULL, cmdline, MAX_PATH), L" delayload");
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi))
    {
        skip("Unable to run test for %s\n", pszModule);
        return FALSE;
    }
    WaitForSingleObject(pi.hProcess, 1000 * 42);
    GetExitCodeProcess(pi.hProcess, &ec);
    if (ec != TESTSUCCESSCODE)
        TerminateProcess(pi.hProcess, 0xDEAD);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    ok(ec == TESTSUCCESSCODE, "%s\n", pszModule);
    return FALSE;
}

#include <uxtheme.h>
#include <inputscope.h> // msctf
#include <objbase.h>
#include <oleacc.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <msiquery.h>
#include <patchapi.h> // mspatcha
#include <winhttp.h>
// #include <wininet.h> Note: We can't do both winhttp and wininet
#include <urlmon.h>
#include <advpub.h>
#include <setupapi.h>
#include <security.h> // secur32 (MSDN)
#include <secext.h> // secur32 (ROS)
#include <cryptuiapi.h>
#include <mscat.h> // wintrust
#include <winnetwk.h> // mpr
#include <dhcpcapi.h> // dhcpcsvc
#include <dhcpcsdk.h> // dhcpcsvc
#include <ws2ipdef.h> // for iphlpapi (sockaddr_in6)
#include <iphlpapi.h>
#include <icmpapi.h> // iphlpapi
#include <lm.h> // netapi32
#include <dbghelp.h>
#include <vfw.h> // msvfw32
#include <usp10.h>
#include <odbcinst.h> // odbccp32
#include <userenv.h>
#include <wincodec.h>
typedef struct _UNICODE_STRING { USHORT Length, MaximumLength; PWCH Buffer;} UNICODE_STRING,*PUNICODE_STRING;
typedef struct { int x; } OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;
#include <ntsam.h>

START_TEST(delayload)
{
    if (GetEnvironmentVariableA(ENVNAME, g_szModule, sizeof(g_szModule)) <= 1)
        *g_szModule = '\0';
    else
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    if (RunTest("gdi32"))
    {
        GetStockObject(NULL_BRUSH);
    }
    else if (RunTest("user32"))
    {
        GetFocus();
    }
    else if (RunTest("uxtheme"))
    {
        IsAppThemed();
    }
    else if (RunTest("imm32"))
    {
        ImmGetDefaultIMEWnd(NULL);
    }
    else if (RunTest("msctf"))
    {
        SetInputScope(NULL, IS_DEFAULT);
    }
    else if (RunTest("comctl32"))
    {
        GetMUILanguage();
    }
    else if (RunTest("comdlg32"))
    {
        CommDlgExtendedError();
    }
    else if (RunTest("ole32"))
    {
        CoTaskMemFree(NULL);
    }
    else if (RunTest("oleaut32"))
    {
        OaBuildVersion();
    }
    else if (RunTest("oleacc"))
    {
        GetRoleTextA(ROLE_SYSTEM_DIALOG, NULL, 0);
    }
    else if (RunTest("shell32"))
    {
        ILFree(NULL);
    }
    else if (RunTest("shlwapi"))
    {
        WCHAR bufw[MAX_PATH];
        DWORD cch = _countof(bufw);
        UrlCreateFromPathW(L"c:\\x", bufw, &cch, 0);
    }
    /* else if (RunTest("shdocvw"))
    {
        // TODO: Add this to shlobj.h
        SoftwareUpdateMessageBox(NULL, L"?", 0, NULL)
    }*/
    /* else if (RunTest("ieframe"))
    {
        // TODO: Add this to iepmapi.h
        HKEY hKey;
        if (SUCCEEDED(IEGetWriteableHKCU(&hKey)))
            RegCloseKey(hKey);
    }*/
    else if (RunTest("msi"))
    {
        MsiGetLanguage((MSIHANDLE)0);
    }
    else if (RunTest("mspatcha"))
    {
        GetFilePatchSignatureW(L"\\?", 0, NULL, 0, NULL, 0, NULL, 0, NULL);
    }
#ifndef InternetOpenUrl
    else if (RunTest("winhttp"))
    {
        SYSTEMTIME st;
        WinHttpTimeToSystemTime(L"", &st);
    }
#else
    else if (RunTest("wininet"))
    {
        InternetCloseHandle(NULL);
    }
#endif
    else if (RunTest("urlmon"))
    {
        CoInternetIsFeatureEnabled(FEATURE_DISABLE_MK_PROTOCOL, GET_FEATURE_FROM_THREAD);
    }
    else if (RunTest("samlib"))
    {
        SamCloseHandle(NULL);
    }
    /* else if (RunTest("samsrv"))
    {
        SamIInitialize();
    }*/

    /* else if (RunTest("lsasrv"))
    {
        LsarClose(NULL);
    }*/
    else if (RunTest("advpack"))
    {
        CloseINFEngine(NULL);
    }
    else if (RunTest("setupapi"))
    {
        InstallHinfSectionA(NULL, NULL, "", SW_HIDE);
    }
    else if (RunTest("secur32"))
    {
        ULONG cch = 0;
        TranslateNameW(L"", NameUnknown, NameUnknown, NULL, &cch);
    }
    else if (RunTest("cryptnet"))
    {
        DWORD cb = 0, cb2 = 0;
        CryptGetObjectUrl(NULL, NULL, 0,NULL, &cb, NULL, &cb2, NULL);
    }
    else if (RunTest("crypt32"))
    {
        CryptGetMessageSignerCount(0, NULL, 0);
    }
    else if (RunTest("cryptui"))
    {
        CRYPTUI_CERT_MGR_STRUCT ccms = { 0 };
        CryptUIDlgCertMgr(&ccms);
    }
    else if (RunTest("wintrust"))
    {
        CryptCATClose(NULL);
    }
    /* else if (RunTest("mlang"))
    {
        // TODO: SDK?
        Rfc1766ToLcidA?
    }*/
    else if (RunTest("mpr"))
    {
        WNetCloseEnum(NULL);
    }
    else if (RunTest("dhcpcsvc"))
    {
        DWORD v;
        if (DhcpCApiInitialize(&v) == ERROR_SUCCESS)
            DhcpCApiCleanup();
    }
    else if (RunTest("iphlpapi"))
    {
        IcmpCloseHandle(NULL);
    }
    else if (RunTest("netapi32"))
    {
        NetApiBufferFree(NULL);
    }
    else if (RunTest("dbghelp"))
    {
        SymGetOptions();
    }
    /* else if (RunTest("apphelp"))
    {
        // TODO: Add this to appcompatapi.h
        ApphelpCheckShellObject
    }*/
    /* else if (RunTest("sfc_os"))
    {
        SfcFileException
    }*/
    else if (RunTest("msvfw32"))
    {
        VideoForWindowsVersion();
    }
    else if (RunTest("usp10"))
    {
        ScriptFreeCache(NULL);
    }
    else if (RunTest("odbccp32"))
    {
        WORD cb = 0;
        SQLReadFileDSN("?", "?", "?", NULL, 0, &cb);
    }
    else if (RunTest("msimg32"))
    {
        TransparentBlt(NULL, 0, 0, 0, 0, NULL, 0, 0, 0, 0, 0);
    }
    else if (RunTest("winmm"))
    {
        joyGetNumDevs();
    }
    else if (RunTest("userenv"))
    {
        DestroyEnvironmentBlock(NULL);
    }
    else if (RunTest("windowscodecs"))
    {
        GUID guid;
        WICMapShortNameToGuid(L"?", &guid);
    }
    else if (RunTest("version"))
    {
        GetFileVersionInfoSizeA("\\?", NULL);
    }
    else
    {
        if (*g_szModule)
            ok(FALSE, "Unknown module %s\n", g_szModule);
    }
    Finish(TRUE);
}

