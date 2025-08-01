/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Winlogon
 * FILE:            base/system/winlogon/sas.c
 * PURPOSE:         Secure Attention Sequence
 * PROGRAMMERS:     Thomas Weidenmueller (w3seek@users.sourceforge.net)
 *                  Hervé Poussineau (hpoussin@reactos.org)
 *                  Arnav Bhatt (arnavbhatt288@gmail.com)
 * UPDATE HISTORY:
 *                  Created 28/03/2004
 */

/* INCLUDES *****************************************************************/

#include "winlogon.h"

#define WIN32_LEAN_AND_MEAN
#include <aclapi.h>
#include <mmsystem.h>
#include <userenv.h>
#include <ndk/setypes.h>
#include <ndk/sefuncs.h>

/* GLOBALS ******************************************************************/

#define WINLOGON_SAS_CLASS L"SAS Window class"
#define WINLOGON_SAS_TITLE L"SAS window"

#define IDHK_CTRL_ALT_DEL   0
#define IDHK_CTRL_SHIFT_ESC 1
#define IDHK_WIN_L          2
#define IDHK_WIN_U          3

// #define EWX_FLAGS_MASK  0x00000014
// #define EWX_ACTION_MASK ~EWX_FLAGS_MASK

// FIXME: At the moment we use this value (select the lowbyte flags and some highbytes ones).
// It should be set such that it makes winlogon accepting only valid flags.
#define EWX_ACTION_MASK 0x5C0F

typedef struct tagLOGOFF_SHUTDOWN_DATA
{
    UINT Flags;
    PWLSESSION Session;
} LOGOFF_SHUTDOWN_DATA, *PLOGOFF_SHUTDOWN_DATA;

static BOOL ExitReactOSInProgress = FALSE;

LUID LuidNone = {0, 0};

typedef struct tagLOGON_SOUND_DATA
{
    HANDLE UserToken;
    BOOL IsStartup;
} LOGON_SOUND_DATA, *PLOGON_SOUND_DATA;

/* FUNCTIONS ****************************************************************/

static BOOL
StartTaskManager(
    IN OUT PWLSESSION Session)
{
    LPVOID lpEnvironment;
    BOOL ret;

    if (!Session->Gina.Functions.WlxStartApplication)
        return FALSE;

    if (!CreateEnvironmentBlock(
        &lpEnvironment,
        Session->UserToken,
        TRUE))
    {
        return FALSE;
    }

    ret = Session->Gina.Functions.WlxStartApplication(
        Session->Gina.Context,
        L"Default",
        lpEnvironment,
        L"taskmgr.exe");

    DestroyEnvironmentBlock(lpEnvironment);
    return ret;
}

static BOOL
StartUserShell(
    IN OUT PWLSESSION Session)
{
    LPVOID lpEnvironment = NULL;
    BOOLEAN Old;
    BOOL ret;

    /* Create environment block for the user */
    if (!CreateEnvironmentBlock(&lpEnvironment, Session->UserToken, TRUE))
    {
        WARN("WL: CreateEnvironmentBlock() failed\n");
        return FALSE;
    }

    /* Get privilege */
    /* FIXME: who should do it? winlogon or gina? */
    /* FIXME: reverting to lower privileges after creating user shell? */
    RtlAdjustPrivilege(SE_ASSIGNPRIMARYTOKEN_PRIVILEGE, TRUE, FALSE, &Old);

    ret = Session->Gina.Functions.WlxActivateUserShell(
                Session->Gina.Context,
                L"Default",
                NULL, /* FIXME */
                lpEnvironment);

    DestroyEnvironmentBlock(lpEnvironment);
    return ret;
}


BOOL
SetDefaultLanguage(
    IN PWLSESSION Session)
{
    BOOL ret = FALSE;
    BOOL UserProfile;
    LONG rc;
    HKEY UserKey, hKey = NULL;
    LPCWSTR SubKey, ValueName;
    DWORD dwType, dwSize;
    LPWSTR Value = NULL;
    UNICODE_STRING ValueString;
    NTSTATUS Status;
    LCID Lcid;

    UserProfile = (Session && Session->UserToken);

    if (UserProfile && !ImpersonateLoggedOnUser(Session->UserToken))
    {
        ERR("WL: ImpersonateLoggedOnUser() failed with error %lu\n", GetLastError());
        return FALSE;
        // FIXME: ... or use the default language of the system??
        // UserProfile = FALSE;
    }

    if (UserProfile)
    {
        rc = RegOpenCurrentUser(MAXIMUM_ALLOWED, &UserKey);
        if (rc != ERROR_SUCCESS)
        {
            TRACE("RegOpenCurrentUser() failed with error %lu\n", rc);
            goto cleanup;
        }

        SubKey = L"Control Panel\\International";
        ValueName = L"Locale";
    }
    else
    {
        UserKey = NULL;
        SubKey = L"System\\CurrentControlSet\\Control\\Nls\\Language";
        ValueName = L"Default";
    }

    rc = RegOpenKeyExW(UserKey ? UserKey : HKEY_LOCAL_MACHINE,
                       SubKey,
                       0,
                       KEY_READ,
                       &hKey);

    if (UserKey)
        RegCloseKey(UserKey);

    if (rc != ERROR_SUCCESS)
    {
        TRACE("RegOpenKeyEx() failed with error %lu\n", rc);
        goto cleanup;
    }

    rc = RegQueryValueExW(hKey,
                          ValueName,
                          NULL,
                          &dwType,
                          NULL,
                          &dwSize);
    if (rc != ERROR_SUCCESS)
    {
        TRACE("RegQueryValueEx() failed with error %lu\n", rc);
        goto cleanup;
    }
    else if (dwType != REG_SZ)
    {
        TRACE("Wrong type for %S\\%S registry entry (got 0x%lx, expected 0x%x)\n",
            SubKey, ValueName, dwType, REG_SZ);
        goto cleanup;
    }

    Value = HeapAlloc(GetProcessHeap(), 0, dwSize);
    if (!Value)
    {
        TRACE("HeapAlloc() failed\n");
        goto cleanup;
    }
    rc = RegQueryValueExW(hKey,
                          ValueName,
                          NULL,
                          NULL,
                          (LPBYTE)Value,
                          &dwSize);
    if (rc != ERROR_SUCCESS)
    {
        TRACE("RegQueryValueEx() failed with error %lu\n", rc);
        goto cleanup;
    }

    /* Convert Value to a Lcid */
    ValueString.Length = ValueString.MaximumLength = (USHORT)dwSize;
    ValueString.Buffer = Value;
    Status = RtlUnicodeStringToInteger(&ValueString, 16, (PULONG)&Lcid);
    if (!NT_SUCCESS(Status))
    {
        TRACE("RtlUnicodeStringToInteger() failed with status 0x%08lx\n", Status);
        goto cleanup;
    }

    TRACE("%s language is 0x%08lx\n",
        UserProfile ? "User" : "System", Lcid);
    Status = NtSetDefaultLocale(UserProfile, Lcid);
    if (!NT_SUCCESS(Status))
    {
        TRACE("NtSetDefaultLocale() failed with status 0x%08lx\n", Status);
        goto cleanup;
    }

    ret = TRUE;

cleanup:
    if (Value)
        HeapFree(GetProcessHeap(), 0, Value);

    if (hKey)
        RegCloseKey(hKey);

    if (UserProfile)
        RevertToSelf();

    return ret;
}

BOOL
PlaySoundRoutine(
    IN LPCWSTR FileName,
    IN UINT bLogon,
    IN UINT Flags)
{
    typedef BOOL (WINAPI *PLAYSOUNDW)(LPCWSTR,HMODULE,DWORD);
    typedef UINT (WINAPI *WAVEOUTGETNUMDEVS)(VOID);
    PLAYSOUNDW Play;
    WAVEOUTGETNUMDEVS waveOutGetNumDevs;
    UINT NumDevs;
    HMODULE hLibrary;
    BOOL Ret = FALSE;

    hLibrary = LoadLibraryW(L"winmm.dll");
    if (!hLibrary)
        return FALSE;

    waveOutGetNumDevs = (WAVEOUTGETNUMDEVS)GetProcAddress(hLibrary, "waveOutGetNumDevs");
    Play = (PLAYSOUNDW)GetProcAddress(hLibrary, "PlaySoundW");

    _SEH2_TRY
    {
        if (waveOutGetNumDevs)
        {
            NumDevs = waveOutGetNumDevs();
            if (!NumDevs)
            {
                if (!bLogon)
                    Beep(440, 125);
                _SEH2_LEAVE;
            }
        }

        if (Play)
            Ret = Play(FileName, NULL, Flags);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ERR("WL: Exception while playing sound '%S', Status 0x%08lx\n",
            FileName ? FileName : L"(n/a)", _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    FreeLibrary(hLibrary);

    return Ret;
}

static
DWORD
WINAPI
PlayLogonSoundThread(
    _In_ LPVOID lpParameter)
{
    PLOGON_SOUND_DATA SoundData = (PLOGON_SOUND_DATA)lpParameter;
    SERVICE_STATUS_PROCESS Info;
    DWORD dwSize;
    ULONG Index = 0;
    SC_HANDLE hSCManager, hService;

    /* Open the service manager */
    hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCManager)
    {
        ERR("OpenSCManager failed (%x)\n", GetLastError());
        goto Cleanup;
    }

    /* Open the wdmaud service */
    hService = OpenServiceW(hSCManager, L"wdmaud", GENERIC_READ);
    if (!hService)
    {
        /* The service is not installed */
        TRACE("Failed to open wdmaud service (%x)\n", GetLastError());
        CloseServiceHandle(hSCManager);
        goto Cleanup;
    }

    /* Wait for wdmaud to start */
    do
    {
        if (!QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&Info, sizeof(SERVICE_STATUS_PROCESS), &dwSize))
        {
            TRACE("QueryServiceStatusEx failed (%x)\n", GetLastError());
            break;
        }

        if (Info.dwCurrentState == SERVICE_RUNNING)
            break;

        Sleep(1000);

    } while (Index++ < 20);

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);

    /* If wdmaud is not running exit */
    if (Info.dwCurrentState != SERVICE_RUNNING)
    {
        WARN("wdmaud has not started!\n");
        goto Cleanup;
    }

    /* Sound subsystem is running. Play logon sound. */
    TRACE("Playing %s sound\n", SoundData->IsStartup ? "startup" : "logon");
    if (!ImpersonateLoggedOnUser(SoundData->UserToken))
    {
        ERR("ImpersonateLoggedOnUser failed (%x)\n", GetLastError());
    }
    else
    {
        PlaySoundRoutine(SoundData->IsStartup ? L"SystemStart" : L"WindowsLogon",
                         TRUE,
                         SND_ALIAS | SND_NODEFAULT);
        RevertToSelf();
    }

Cleanup:
    HeapFree(GetProcessHeap(), 0, SoundData);
    return 0;
}

static
VOID
PlayLogonSound(
    _In_ PWLSESSION Session)
{
    PLOGON_SOUND_DATA SoundData;
    HANDLE hThread;

    SoundData = HeapAlloc(GetProcessHeap(), 0, sizeof(LOGON_SOUND_DATA));
    if (!SoundData)
        return;

    SoundData->UserToken = Session->UserToken;
    SoundData->IsStartup = IsFirstLogon(Session);

    hThread = CreateThread(NULL, 0, PlayLogonSoundThread, SoundData, 0, NULL);
    if (!hThread)
    {
        HeapFree(GetProcessHeap(), 0, SoundData);
        return;
    }
    CloseHandle(hThread);
}

static
VOID
PlayLogoffShutdownSound(
    _In_ PWLSESSION Session,
    _In_ BOOL bShutdown)
{
    if (!ImpersonateLoggedOnUser(Session->UserToken))
        return;

    /* NOTE: Logoff and shutdown sounds play synchronously */
    PlaySoundRoutine(bShutdown ? L"SystemExit" : L"WindowsLogoff",
                     FALSE,
                     SND_ALIAS | SND_NODEFAULT);

    RevertToSelf();
}

static
BOOL
PlayEventSound(
    _In_ PWLSESSION Session,
    _In_ LPCWSTR EventName)
{
    BOOL bRet;

    if (!ImpersonateLoggedOnUser(Session->UserToken))
        return FALSE;

    bRet = PlaySoundRoutine(EventName, FALSE, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);

    RevertToSelf();

    return bRet;
}

static
VOID
RestoreAllConnections(PWLSESSION Session)
{
    DWORD dRet;
    HANDLE hEnum;
    LPNETRESOURCE lpRes;
    DWORD dSize = 0x1000;
    DWORD dCount = -1;
    LPNETRESOURCE lpCur;
    BOOL UserProfile;

    UserProfile = (Session && Session->UserToken);
    if (!UserProfile)
    {
        return;
    }

    if (!ImpersonateLoggedOnUser(Session->UserToken))
    {
        ERR("WL: ImpersonateLoggedOnUser() failed with error %lu\n", GetLastError());
        return;
    }

    dRet = WNetOpenEnum(RESOURCE_REMEMBERED, RESOURCETYPE_DISK, 0, NULL, &hEnum);
    if (dRet != WN_SUCCESS)
    {
        ERR("Failed to open enumeration: %lu\n", dRet);
        goto quit;
    }

    lpRes = HeapAlloc(GetProcessHeap(), 0, dSize);
    if (!lpRes)
    {
        ERR("Failed to allocate memory\n");
        WNetCloseEnum(hEnum);
        goto quit;
    }

    do
    {
        dSize = 0x1000;
        dCount = -1;

        memset(lpRes, 0, dSize);
        dRet = WNetEnumResource(hEnum, &dCount, lpRes, &dSize);
        if (dRet == WN_SUCCESS || dRet == WN_MORE_DATA)
        {
            lpCur = lpRes;
            for (; dCount; dCount--)
            {
                WNetAddConnection(lpCur->lpRemoteName, NULL, lpCur->lpLocalName);
                lpCur++;
            }
        }
    } while (dRet != WN_NO_MORE_ENTRIES);

    HeapFree(GetProcessHeap(), 0, lpRes);
    WNetCloseEnum(hEnum);

quit:
    RevertToSelf();
}

/**
 * @brief
 * Frees the Profile information structure (WLX_PROFILE_V1_0
 * or WLX_PROFILE_V2_0) allocated by the GINA.
 **/
static VOID
FreeWlxProfileInfo(
    _Inout_ PVOID Profile)
{
    PWLX_PROFILE_V2_0 pProfile = (PWLX_PROFILE_V2_0)Profile;

    if (pProfile->dwType != WLX_PROFILE_TYPE_V1_0
     && pProfile->dwType != WLX_PROFILE_TYPE_V2_0)
    {
        ERR("WL: Wrong profile info\n");
        return;
    }

    if (pProfile->pszProfile)
        LocalFree(pProfile->pszProfile);
    if (pProfile->dwType >= WLX_PROFILE_TYPE_V2_0)
    {
        if (pProfile->pszPolicy)
            LocalFree(pProfile->pszPolicy);
        if (pProfile->pszNetworkDefaultUserProfile)
            LocalFree(pProfile->pszNetworkDefaultUserProfile);
        if (pProfile->pszServerName)
            LocalFree(pProfile->pszServerName);
        if (pProfile->pszEnvironment)
            LocalFree(pProfile->pszEnvironment);
    }
}

/**
 * @brief
 * Frees the MPR information structure allocated by the GINA.
 *
 * @note
 * Currently used only in HandleLogon(), but will also be used
 * by WlxChangePasswordNotify(Ex) once implemented.
 **/
static VOID
FreeWlxMprInfo(
    _Inout_ PWLX_MPR_NOTIFY_INFO MprNotifyInfo)
{
    if (MprNotifyInfo->pszUserName)
        LocalFree(MprNotifyInfo->pszUserName);
    if (MprNotifyInfo->pszDomain)
        LocalFree(MprNotifyInfo->pszDomain);
    if (MprNotifyInfo->pszPassword)
    {
        /* Zero out the password buffer before freeing it */
        SIZE_T pwdLen = (wcslen(MprNotifyInfo->pszPassword) + 1) * sizeof(WCHAR);
        SecureZeroMemory(MprNotifyInfo->pszPassword, pwdLen);
        LocalFree(MprNotifyInfo->pszPassword);
    }
    if (MprNotifyInfo->pszOldPassword)
    {
        /* Zero out the password buffer before freeing it */
        SIZE_T pwdLen = (wcslen(MprNotifyInfo->pszOldPassword) + 1) * sizeof(WCHAR);
        SecureZeroMemory(MprNotifyInfo->pszOldPassword, pwdLen);
        LocalFree(MprNotifyInfo->pszOldPassword);
    }
}

static
BOOL
HandleLogon(
    IN OUT PWLSESSION Session)
{
    BOOL ret = FALSE;

    /* Loading personal settings */
    DisplayStatusMessage(Session, Session->WinlogonDesktop, IDS_LOADINGYOURPERSONALSETTINGS);

    Session->hProfileInfo = NULL;
    if (!(Session->Options & WLX_LOGON_OPT_NO_PROFILE))
    {
        HKEY hKey;
        LONG lError;
        BOOL bNoPopups = FALSE;
        PROFILEINFOW ProfileInfo;

        if (Session->Profile == NULL
         || (Session->Profile->dwType != WLX_PROFILE_TYPE_V1_0
          && Session->Profile->dwType != WLX_PROFILE_TYPE_V2_0))
        {
            ERR("WL: Wrong profile\n");
            goto cleanup;
        }

        /* Check whether error messages may be displayed when loading the user profile */
        lError = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                               L"System\\CurrentControlSet\\Control\\Windows",
                               0,
                               KEY_QUERY_VALUE,
                               &hKey);
        if (lError == ERROR_SUCCESS)
        {
            DWORD dwValue, dwType, cbData = sizeof(dwValue);
            lError = RegQueryValueExW(hKey, L"NoPopupsOnBoot", NULL,
                                      &dwType, (PBYTE)&dwValue, &cbData);
            if ((lError == ERROR_SUCCESS) && (dwType == REG_DWORD) && (cbData == sizeof(dwValue)))
                bNoPopups = !!dwValue;

            RegCloseKey(hKey);
        }

        /* Load the user profile */
        ZeroMemory(&ProfileInfo, sizeof(ProfileInfo));
        ProfileInfo.dwSize = sizeof(ProfileInfo);
        if (bNoPopups)
            ProfileInfo.dwFlags |= PI_NOUI;
        ProfileInfo.lpUserName = Session->MprNotifyInfo.pszUserName;
        ProfileInfo.lpProfilePath = Session->Profile->pszProfile;
        if (Session->Profile->dwType >= WLX_PROFILE_TYPE_V2_0)
        {
            ProfileInfo.lpDefaultPath = Session->Profile->pszNetworkDefaultUserProfile;
            ProfileInfo.lpServerName = Session->Profile->pszServerName;
            ProfileInfo.lpPolicyPath = Session->Profile->pszPolicy;
        }

        if (!LoadUserProfileW(Session->UserToken, &ProfileInfo))
        {
            ERR("WL: LoadUserProfileW() failed\n");
            goto cleanup;
        }
        Session->hProfileInfo = ProfileInfo.hProfile;
    }

    /* Create environment block for the user */
    if (!CreateUserEnvironment(Session))
    {
        WARN("WL: CreateUserEnvironment() failed\n");
        goto cleanup;
    }

    CallNotificationDlls(Session, LogonHandler);

    /* Enable per-user settings */
    DisplayStatusMessage(Session, Session->WinlogonDesktop, IDS_APPLYINGYOURPERSONALSETTINGS);
    UpdatePerUserSystemParameters(0, TRUE);

    /* Set default user language */
    if (!SetDefaultLanguage(Session))
    {
        WARN("WL: SetDefaultLanguage() failed\n");
        goto cleanup;
    }

    /* Allow winsta and desktop access for this session */
    if (!AllowAccessOnSession(Session))
    {
        WARN("WL: AllowAccessOnSession() failed to give winsta & desktop access for this session\n");
        goto cleanup;
    }

    /* Connect remote resources */
    RestoreAllConnections(Session);

    if (!StartUserShell(Session))
    {
        //WCHAR StatusMsg[256];
        WARN("WL: WlxActivateUserShell() failed\n");
        //LoadStringW(hAppInstance, IDS_FAILEDACTIVATEUSERSHELL, StatusMsg, sizeof(StatusMsg) / sizeof(StatusMsg[0]));
        //MessageBoxW(0, StatusMsg, NULL, MB_ICONERROR);
        goto cleanup;
    }

    CallNotificationDlls(Session, StartShellHandler);

    if (!InitializeScreenSaver(Session))
        WARN("WL: Failed to initialize screen saver\n");

    /* Logon has succeeded. Play sound. */
    PlayLogonSound(Session);

    /* NOTE: The logon timestamp has to be set after calling PlayLogonSound
     * to correctly detect the startup event (first logon) */
    SetLogonTimestamp(Session);
    ret = TRUE;

cleanup:
    if (Session->Profile)
    {
        FreeWlxProfileInfo(Session->Profile);
        LocalFree(Session->Profile);
        Session->Profile = NULL;
    }
    FreeWlxMprInfo(&Session->MprNotifyInfo);
    ZeroMemory(&Session->MprNotifyInfo, sizeof(Session->MprNotifyInfo));

    RemoveStatusMessage(Session);

    if (!ret)
    {
        if (Session->hProfileInfo)
            UnloadUserProfile(Session->UserToken, Session->hProfileInfo);
        Session->hProfileInfo = NULL;

        /* Restore default system parameters */
        UpdatePerUserSystemParameters(0, FALSE);

        // TODO: Remove session access to window station
        // (revert what security.c!AllowAccessOnSession() does).
        SetWindowStationUser(Session->InteractiveWindowStation,
                             &LuidNone, NULL, 0);

        /* Switch back to default SYSTEM user */
        CloseHandle(Session->UserToken);
        Session->UserToken = NULL;
        Session->LogonId = LuidNone;
    }
    else // if (ret)
    {
        SwitchDesktop(Session->ApplicationDesktop);
        Session->LogonState = STATE_LOGGED_ON;
    }
    return ret;
}


static
DWORD
WINAPI
LogoffShutdownThread(
    LPVOID Parameter)
{
    PLOGOFF_SHUTDOWN_DATA LSData = (PLOGOFF_SHUTDOWN_DATA)Parameter;
    HANDLE UserToken = LSData->Session->UserToken;
    DWORD ret = TRUE;
    UINT uFlags;

    if (UserToken && !ImpersonateLoggedOnUser(UserToken))
    {
        ERR("ImpersonateLoggedOnUser() failed with error %lu\n", GetLastError());
        return FALSE;
    }

    // FIXME: To be really fixed: need to check what needs to be kept and what needs to be removed there.
    //
    // uFlags = EWX_INTERNAL_KILL_USER_APPS | (LSData->Flags & EWX_FLAGS_MASK) |
             // ((LSData->Flags & EWX_ACTION_MASK) == EWX_LOGOFF ? EWX_CALLER_WINLOGON_LOGOFF : 0);

    uFlags = EWX_CALLER_WINLOGON | (LSData->Flags & 0x0F);

    TRACE("In LogoffShutdownThread with uFlags == 0x%x; exit_in_progress == %s\n",
        uFlags, ExitReactOSInProgress ? "TRUE" : "FALSE");

    ExitReactOSInProgress = TRUE;

    /* Close processes of the interactive user */
    if (!ExitWindowsEx(uFlags, 0))
    {
        ERR("Unable to kill user apps, error %lu\n", GetLastError());
        ret = FALSE;
    }

    /* Cancel all the user connections */
    WNetClearConnections(NULL);

    if (UserToken)
        RevertToSelf();

    return ret;
}

static
NTSTATUS
RunLogoffShutdownThread(
    _In_ PWLSESSION Session,
    _In_opt_ PSECURITY_ATTRIBUTES psa,
    _In_ DWORD wlxAction)
{
    PCSTR pDescName;
    PLOGOFF_SHUTDOWN_DATA LSData;
    HANDLE hThread;
    DWORD dwExitCode;

    /* Validate the action */
    if (WLX_LOGGINGOFF(wlxAction))
    {
        pDescName = "Logoff";
    }
    else if (WLX_SHUTTINGDOWN(wlxAction))
    {
        pDescName = "Shutdown";
    }
    else
    {
        ASSERT(FALSE);
        return STATUS_INVALID_PARAMETER;
    }

    /* Prepare data for the logoff/shutdown thread */
    LSData = HeapAlloc(GetProcessHeap(), 0, sizeof(*LSData));
    if (!LSData)
    {
        ERR("Failed to allocate %s thread data\n", pDescName);
        return STATUS_NO_MEMORY;
    }

    /* Set the flags accordingly */
    if (WLX_LOGGINGOFF(wlxAction))
    {
        LSData->Flags = EWX_LOGOFF;
        if (wlxAction == WLX_SAS_ACTION_FORCE_LOGOFF)
            LSData->Flags |= EWX_FORCE;
    }
    else // if (WLX_SHUTTINGDOWN(wlxAction))
    {
        /* Because we are shutting down the OS, force processes termination too */
        LSData->Flags = EWX_SHUTDOWN | EWX_FORCE;
        if (wlxAction == WLX_SAS_ACTION_SHUTDOWN_POWER_OFF)
            LSData->Flags |= EWX_POWEROFF;
        else if (wlxAction == WLX_SAS_ACTION_SHUTDOWN_REBOOT)
            LSData->Flags |= EWX_REBOOT;
    }

    LSData->Session = Session;

    /* Run the logoff/shutdown thread */
    hThread = CreateThread(psa, 0, LogoffShutdownThread, LSData, 0, NULL);
    if (!hThread)
    {
        ERR("Unable to create %s thread, error %lu\n", pDescName, GetLastError());
        HeapFree(GetProcessHeap(), 0, LSData);
        return STATUS_UNSUCCESSFUL;
    }
    WaitForSingleObject(hThread, INFINITE);
    HeapFree(GetProcessHeap(), 0, LSData);
    if (!GetExitCodeThread(hThread, &dwExitCode))
    {
        ERR("Unable to get %s thread exit code (error %lu)\n", pDescName, GetLastError());
        CloseHandle(hThread);
        return STATUS_UNSUCCESSFUL;
    }
    CloseHandle(hThread);
    if (dwExitCode == 0)
    {
        ERR("%s thread returned failure\n", pDescName);
        return STATUS_UNSUCCESSFUL;
    }
    return STATUS_SUCCESS;
}

static
DWORD
WINAPI
KillComProcesses(
    LPVOID Parameter)
{
    HANDLE UserToken = (HANDLE)Parameter;
    DWORD ret = TRUE;

    TRACE("In KillComProcesses\n");

    if (UserToken && !ImpersonateLoggedOnUser(UserToken))
    {
        ERR("ImpersonateLoggedOnUser() failed with error %lu\n", GetLastError());
        return FALSE;
    }

    /* Attempt to kill remaining processes. No notifications needed. */
    if (!ExitWindowsEx(EWX_CALLER_WINLOGON | EWX_NONOTIFY | EWX_FORCE | EWX_LOGOFF, 0))
    {
        ERR("Unable to kill COM apps, error %lu\n", GetLastError());
        ret = FALSE;
    }

    if (UserToken)
        RevertToSelf();

    return ret;
}


static
NTSTATUS
CreateLogoffSecurityAttributes(
    OUT PSECURITY_ATTRIBUTES* ppsa)
{
    /* The following code is not working yet and messy */
    /* Still, it gives some ideas about data types and functions involved and */
    /* required to set up a SECURITY_DESCRIPTOR for a SECURITY_ATTRIBUTES */
    /* instance for a thread, to allow that  thread to ImpersonateLoggedOnUser(). */
    /* Specifically THREAD_SET_THREAD_TOKEN is required. */
    PSECURITY_DESCRIPTOR SecurityDescriptor = NULL;
    PSECURITY_ATTRIBUTES psa = 0;
    BYTE* pMem;
    PACL pACL;
    EXPLICIT_ACCESS Access;
    PSID pEveryoneSID = NULL;
    static SID_IDENTIFIER_AUTHORITY WorldAuthority = { SECURITY_WORLD_SID_AUTHORITY };

    *ppsa = NULL;

    // Let's first try to enumerate what kind of data we need for this to ever work:
    // 1.  The Winlogon SID, to be able to give it THREAD_SET_THREAD_TOKEN.
    // 2.  The users SID (the user trying to logoff, or rather shut down the system).
    // 3.  At least two EXPLICIT_ACCESS instances:
    // 3.1 One for Winlogon itself, giving it the rights
    //     required to THREAD_SET_THREAD_TOKEN (as it's needed to successfully call
    //     ImpersonateLoggedOnUser).
    // 3.2 One for the user, to allow *that* thread to perform its work.
    // 4.  An ACL to hold the these EXPLICIT_ACCESS ACE's.
    // 5.  A SECURITY_DESCRIPTOR to hold the ACL, and finally.
    // 6.  A SECURITY_ATTRIBUTES instance to pull all of this required stuff
    //     together, to hand it to CreateThread.
    //
    // However, it seems struct LOGOFF_SHUTDOWN_DATA doesn't contain
    // these required SID's, why they'd have to be added.
    // The Winlogon's own SID should probably only be created once,
    // while the user's SID obviously must be created for each new user.
    // Might as well store it when the user logs on?

    if(!AllocateAndInitializeSid(&WorldAuthority,
                                 1,
                                 SECURITY_WORLD_RID,
                                 0, 0, 0, 0, 0, 0, 0,
                                 &pEveryoneSID))
    {
        ERR("Failed to initialize security descriptor for logoff thread!\n");
        return STATUS_UNSUCCESSFUL;
    }

    /* set up the required security attributes to be able to shut down */
    /* To save space and time, allocate a single block of memory holding */
    /* both SECURITY_ATTRIBUTES and SECURITY_DESCRIPTOR */
    pMem = HeapAlloc(GetProcessHeap(),
                     0,
                     sizeof(SECURITY_ATTRIBUTES) +
                     SECURITY_DESCRIPTOR_MIN_LENGTH +
                     sizeof(ACL));
    if (!pMem)
    {
        ERR("Failed to allocate memory for logoff security descriptor!\n");
        return STATUS_NO_MEMORY;
    }

    /* Note that the security descriptor needs to be in _absolute_ format, */
    /* meaning its members must be pointers to other structures, rather */
    /* than the relative format using offsets */
    psa = (PSECURITY_ATTRIBUTES)pMem;
    SecurityDescriptor = (PSECURITY_DESCRIPTOR)(pMem + sizeof(SECURITY_ATTRIBUTES));
    pACL = (PACL)(((PBYTE)SecurityDescriptor) + SECURITY_DESCRIPTOR_MIN_LENGTH);

    // Initialize an EXPLICIT_ACCESS structure for an ACE.
    // The ACE will allow this thread to log off (and shut down the system, currently).
    ZeroMemory(&Access, sizeof(Access));
    Access.grfAccessPermissions = THREAD_SET_THREAD_TOKEN;
    Access.grfAccessMode = SET_ACCESS; // GRANT_ACCESS?
    Access.grfInheritance = NO_INHERITANCE;
    Access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    Access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    Access.Trustee.ptstrName = pEveryoneSID;

    if (SetEntriesInAcl(1, &Access, NULL, &pACL) != ERROR_SUCCESS)
    {
        ERR("Failed to set Access Rights for logoff thread. Logging out will most likely fail.\n");

        HeapFree(GetProcessHeap(), 0, pMem);
        return STATUS_UNSUCCESSFUL;
    }

    if (!InitializeSecurityDescriptor(SecurityDescriptor, SECURITY_DESCRIPTOR_REVISION))
    {
        ERR("Failed to initialize security descriptor for logoff thread!\n");
        HeapFree(GetProcessHeap(), 0, pMem);
        return STATUS_UNSUCCESSFUL;
    }

    if (!SetSecurityDescriptorDacl(SecurityDescriptor,
                                   TRUE,     // bDaclPresent flag
                                   pACL,
                                   FALSE))   // not a default DACL
    {
        ERR("SetSecurityDescriptorDacl Error %lu\n", GetLastError());
        HeapFree(GetProcessHeap(), 0, pMem);
        return STATUS_UNSUCCESSFUL;
    }

    psa->nLength = sizeof(SECURITY_ATTRIBUTES);
    psa->lpSecurityDescriptor = SecurityDescriptor;
    psa->bInheritHandle = FALSE;

    *ppsa = psa;

    return STATUS_SUCCESS;
}

static
VOID
DestroyLogoffSecurityAttributes(
    IN PSECURITY_ATTRIBUTES psa)
{
    if (psa)
    {
        HeapFree(GetProcessHeap(), 0, psa);
    }
}


static
NTSTATUS
HandleLogoff(
    _Inout_ PWLSESSION Session,
    _In_ DWORD wlxAction)
{
    PSECURITY_ATTRIBUTES psa;
    HANDLE hThread;
    NTSTATUS Status;

    Status = CreateLogoffSecurityAttributes(&psa);
    if (!NT_SUCCESS(Status))
    {
        ERR("Failed to create Logoff security descriptor. Status 0x%08lx\n", Status);
        return Status;
    }

    /* Run the Logoff thread. Log off as well if we are
     * invoked as part of a shutdown operation. */
    Status = RunLogoffShutdownThread(Session, psa,
                                     WLX_LOGGINGOFF(wlxAction)
                                        ? wlxAction
                                        : WLX_SAS_ACTION_LOGOFF);
    if (!NT_SUCCESS(Status))
    {
        ERR("Failed to start the Logoff thread, Status 0x%08lx\n", Status);
        DestroyLogoffSecurityAttributes(psa);
        return Status;
    }

    SwitchDesktop(Session->WinlogonDesktop);

    PlayLogoffShutdownSound(Session, WLX_SHUTTINGDOWN(wlxAction));

    SetWindowStationUser(Session->InteractiveWindowStation,
                         &LuidNone, NULL, 0);

    // DisplayStatusMessage(Session, Session->WinlogonDesktop, IDS_LOGGINGOFF);
    CallNotificationDlls(Session, LogoffHandler);

    // FIXME: Closing network connections!
    // DisplayStatusMessage(Session, Session->WinlogonDesktop, IDS_CLOSINGNETWORKCONNECTIONS);

    /* Kill remaining COM processes that may have been started by logoff scripts */
    hThread = CreateThread(psa, 0, KillComProcesses, (PVOID)Session->UserToken, 0, NULL);
    if (hThread)
    {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }

    /* We're done with the SECURITY_DESCRIPTOR */
    DestroyLogoffSecurityAttributes(psa);

    DisplayStatusMessage(Session, Session->WinlogonDesktop, IDS_SAVEYOURSETTINGS);

    if (Session->hProfileInfo)
        UnloadUserProfile(Session->UserToken, Session->hProfileInfo);
    Session->hProfileInfo = NULL;

    /* Restore default system parameters */
    UpdatePerUserSystemParameters(0, FALSE);

    // TODO: Remove session access to window station
    // (revert what security.c!AllowAccessOnSession() does).

    /* Switch back to default SYSTEM user */
    CloseHandle(Session->UserToken);
    Session->UserToken = NULL;
    Session->LogonId = LuidNone;

    Session->LogonState = STATE_LOGGED_OFF;

    return STATUS_SUCCESS;
}

static
INT_PTR
CALLBACK
ShutdownComputerWindowProc(
    IN HWND hwndDlg,
    IN UINT uMsg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (uMsg)
    {
        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case IDC_BTNSHTDOWNCOMPUTER:
                    EndDialog(hwndDlg, IDC_BTNSHTDOWNCOMPUTER);
                    return TRUE;
            }
            break;
        }
        case WM_INITDIALOG:
        {
            RemoveMenu(GetSystemMenu(hwndDlg, FALSE), SC_CLOSE, MF_BYCOMMAND);
            SetFocus(GetDlgItem(hwndDlg, IDC_BTNSHTDOWNCOMPUTER));
            return TRUE;
        }
    }
    return FALSE;
}

static
VOID
UninitializeSAS(
    IN OUT PWLSESSION Session)
{
    if (Session->SASWindow)
    {
        DestroyWindow(Session->SASWindow);
        Session->SASWindow = NULL;
    }
    if (Session->hEndOfScreenSaverThread)
        SetEvent(Session->hEndOfScreenSaverThread);
    UnregisterClassW(WINLOGON_SAS_CLASS, hAppInstance);
}

NTSTATUS
HandleShutdown(
    IN OUT PWLSESSION Session,
    IN DWORD wlxAction)
{
    NTSTATUS Status;
    BOOLEAN Old;

    // SwitchDesktop(Session->WinlogonDesktop);

    /* If the system is rebooting, show the appropriate string */
    if (wlxAction == WLX_SAS_ACTION_SHUTDOWN_REBOOT)
        DisplayStatusMessage(Session, Session->WinlogonDesktop, IDS_REACTOSISRESTARTING);
    else
        DisplayStatusMessage(Session, Session->WinlogonDesktop, IDS_REACTOSISSHUTTINGDOWN);

    /* Run the shutdown thread. *IGNORE* all failures as we want to force shutting down! */
    Status = RunLogoffShutdownThread(Session, NULL, wlxAction);
    if (!NT_SUCCESS(Status))
        ERR("Failed to start the Shutdown thread, Status 0x%08lx\n", Status);

    CallNotificationDlls(Session, ShutdownHandler);

    /* Destroy SAS window */
    UninitializeSAS(Session);

    /* Now we can shut down NT */
    ERR("Shutting down NT...\n");
    RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, TRUE, FALSE, &Old);
    if (wlxAction == WLX_SAS_ACTION_SHUTDOWN_REBOOT)
    {
        NtShutdownSystem(ShutdownReboot);
    }
    else
    {
        if (FALSE)
        {
            /* FIXME - only show this dialog if it's a shutdown and the computer doesn't support APM */
            DialogBox(hAppInstance, MAKEINTRESOURCE(IDD_SHUTDOWNCOMPUTER),
                      GetDesktopWindow(), ShutdownComputerWindowProc);
        }
        if (wlxAction == WLX_SAS_ACTION_SHUTDOWN_POWER_OFF)
            NtShutdownSystem(ShutdownPowerOff);
        else // if (wlxAction == WLX_SAS_ACTION_SHUTDOWN)
            NtShutdownSystem(ShutdownNoReboot);
    }
    RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, Old, FALSE, &Old);
    return STATUS_SUCCESS;
}

static
VOID
DoGenericAction(
    IN OUT PWLSESSION Session,
    IN DWORD wlxAction)
{
    switch (wlxAction)
    {
        case WLX_SAS_ACTION_LOGON: /* 0x01 */
            if (Session->LogonState == STATE_LOGGED_OFF_SAS)
            {
                if (!HandleLogon(Session))
                {
                    Session->LogonState = STATE_LOGGED_OFF;
                    Session->Gina.Functions.WlxDisplaySASNotice(Session->Gina.Context);
                    CallNotificationDlls(Session, LogonHandler);
                }
            }
            break;
        case WLX_SAS_ACTION_NONE: /* 0x02 */
            if (Session->LogonState == STATE_LOGGED_OFF_SAS)
            {
                Session->LogonState = STATE_LOGGED_OFF;
                Session->Gina.Functions.WlxDisplaySASNotice(Session->Gina.Context);
            }
            else if (Session->LogonState == STATE_LOGGED_ON_SAS)
            {
                Session->LogonState = STATE_LOGGED_ON;
            }
            else if (Session->LogonState == STATE_LOCKED_SAS)
            {
                Session->LogonState = STATE_LOCKED;
                Session->Gina.Functions.WlxDisplayLockedNotice(Session->Gina.Context);
            }
            break;
        case WLX_SAS_ACTION_LOCK_WKSTA: /* 0x03 */
            if ((Session->LogonState == STATE_LOGGED_ON) ||
                (Session->LogonState == STATE_LOGGED_ON_SAS))
            {
                if (Session->Gina.Functions.WlxIsLockOk(Session->Gina.Context))
                {
                    Session->LogonState = STATE_LOCKED;
                    SwitchDesktop(Session->WinlogonDesktop);
                    /* We may be on the Logged-On SAS dialog, in which case
                     * we need to close it if the lock action came via Win-L */
                    CloseAllDialogWindows();
                    CallNotificationDlls(Session, LockHandler);
                    Session->Gina.Functions.WlxDisplayLockedNotice(Session->Gina.Context);
                }
            }
            break;
        case WLX_SAS_ACTION_LOGOFF: /* 0x04 */
        case WLX_SAS_ACTION_SHUTDOWN: /* 0x05 */
        case WLX_SAS_ACTION_FORCE_LOGOFF: /* 0x09 */
        case WLX_SAS_ACTION_SHUTDOWN_POWER_OFF: /* 0x0a */
        case WLX_SAS_ACTION_SHUTDOWN_REBOOT: /* 0x0b */
            if (Session->LogonState != STATE_LOGGED_OFF)
            {
                if (!Session->Gina.Functions.WlxIsLogoffOk(Session->Gina.Context))
                    break;
                if (!NT_SUCCESS(HandleLogoff(Session, wlxAction)))
                {
                    RemoveStatusMessage(Session);
                    break;
                }
                Session->Gina.Functions.WlxLogoff(Session->Gina.Context);
            }
            if (WLX_SHUTTINGDOWN(wlxAction))
            {
                // FIXME: WlxShutdown should be done from inside HandleShutdown,
                // after having displayed "ReactOS is shutting down" message.
                Session->Gina.Functions.WlxShutdown(Session->Gina.Context, wlxAction);
                if (!NT_SUCCESS(HandleShutdown(Session, wlxAction)))
                {
                    RemoveStatusMessage(Session);
                    Session->LogonState = STATE_LOGGED_OFF;
                    Session->Gina.Functions.WlxDisplaySASNotice(Session->Gina.Context);
                }
            }
            else
            {
                RemoveStatusMessage(Session);
                Session->LogonState = STATE_LOGGED_OFF;
                Session->Gina.Functions.WlxDisplaySASNotice(Session->Gina.Context);
            }
            break;
        case WLX_SAS_ACTION_TASKLIST: /* 0x07 */
            if ((Session->LogonState == STATE_LOGGED_ON) ||
                (Session->LogonState == STATE_LOGGED_ON_SAS))
            {
                /* Start a Task-Manager instance on the application desktop.
                 * If the user pressed Ctrl-Shift-Esc while being on the
                 * Logged-On SAS dialog (on the Winlogon desktop), stay there. */
                StartTaskManager(Session);
            }
            break;
        case WLX_SAS_ACTION_UNLOCK_WKSTA: /* 0x08 */
            if ((Session->LogonState == STATE_LOCKED) ||
                (Session->LogonState == STATE_LOCKED_SAS))
            {
                CallNotificationDlls(Session, UnlockHandler);
                SwitchDesktop(Session->ApplicationDesktop);
                Session->LogonState = STATE_LOGGED_ON;
            }
            break;
        default:
            WARN("Unknown SAS action 0x%lx\n", wlxAction);
    }
}

static
VOID
DispatchSAS(
    IN OUT PWLSESSION Session,
    IN DWORD dwSasType)
{
    DWORD wlxAction = WLX_SAS_ACTION_NONE;
    PSID LogonSid = NULL; /* FIXME */
    BOOL bSecure = TRUE;

    switch (dwSasType)
    {
        case WLX_SAS_TYPE_CTRL_ALT_DEL:
            switch (Session->LogonState)
            {
                case STATE_INIT:
                    Session->LogonState = STATE_LOGGED_OFF;
                    Session->Gina.Functions.WlxDisplaySASNotice(Session->Gina.Context);
                    return;

                case STATE_LOGGED_OFF:
                    Session->LogonState = STATE_LOGGED_OFF_SAS;

                    CloseAllDialogWindows();

                    Session->Options = 0;

                    wlxAction = (DWORD)Session->Gina.Functions.WlxLoggedOutSAS(
                        Session->Gina.Context,
                        Session->SASAction,
                        &Session->LogonId,
                        LogonSid,
                        &Session->Options,
                        &Session->UserToken,
                        &Session->MprNotifyInfo,
                        (PVOID*)&Session->Profile);
                    break;

                case STATE_LOGGED_OFF_SAS:
                    /* Ignore SAS if we are already in an SAS state */
                    return;

                case STATE_LOGGED_ON:
                    Session->LogonState = STATE_LOGGED_ON_SAS;
                    SwitchDesktop(Session->WinlogonDesktop);
                    wlxAction = (DWORD)Session->Gina.Functions.WlxLoggedOnSAS(Session->Gina.Context, dwSasType, NULL);
                    if ((wlxAction == WLX_SAS_ACTION_NONE) ||
                        (wlxAction == WLX_SAS_ACTION_TASKLIST))
                    {
                        /*
                         * If the user canceled (WLX_SAS_ACTION_NONE) the
                         * Logged-On SAS dialog, or clicked on the Task-Manager
                         * button (WLX_SAS_ACTION_TASKLIST), switch back to
                         * the application desktop and return to log-on state.
                         * In the latter case, the Task-Manager is launched
                         * by DoGenericAction(WLX_SAS_ACTION_TASKLIST), which
                         * doesn't automatically do the switch back, because
                         * the user may have also pressed on Ctrl-Shift-Esc
                         * to start it while being on the Logged-On SAS dialog
                         * and wanting to stay there.
                         */
                        SwitchDesktop(Session->ApplicationDesktop);
                        Session->LogonState = STATE_LOGGED_ON;
                    }
                    break;

                case STATE_LOGGED_ON_SAS:
                    /* Ignore SAS if we are already in an SAS state */
                    return;

                case STATE_LOCKED:
                    Session->LogonState = STATE_LOCKED_SAS;

                    CloseAllDialogWindows();

                    wlxAction = (DWORD)Session->Gina.Functions.WlxWkstaLockedSAS(Session->Gina.Context, dwSasType);
                    break;

                case STATE_LOCKED_SAS:
                    /* Ignore SAS if we are already in an SAS state */
                    return;

                default:
                    return;
            }
            break;

        case WLX_SAS_TYPE_TIMEOUT:
            return;

        case WLX_SAS_TYPE_SCRNSVR_TIMEOUT:
            if (!Session->Gina.Functions.WlxScreenSaverNotify(Session->Gina.Context, &bSecure))
            {
                /* Skip start of screen saver */
                SetEvent(Session->hEndOfScreenSaver);
            }
            else
            {
                StartScreenSaver(Session);
                if (bSecure)
                {
                    wlxAction = WLX_SAS_ACTION_LOCK_WKSTA;
//                    DoGenericAction(Session, WLX_SAS_ACTION_LOCK_WKSTA);
                }
            }
            break;

        case WLX_SAS_TYPE_SCRNSVR_ACTIVITY:
            SetEvent(Session->hUserActivity);
            break;
    }

    DoGenericAction(Session, wlxAction);
}

static
BOOL
RegisterHotKeys(
    IN PWLSESSION Session,
    IN HWND hwndSAS)
{
    /* Register Ctrl+Alt+Del hotkey */
    if (!RegisterHotKey(hwndSAS, IDHK_CTRL_ALT_DEL, MOD_CONTROL | MOD_ALT, VK_DELETE))
    {
        ERR("WL: Unable to register Ctrl+Alt+Del hotkey\n");
        return FALSE;
    }

    /* Register Ctrl+Shift+Esc "Task Manager" hotkey (optional) */
    Session->TaskManHotkey = RegisterHotKey(hwndSAS, IDHK_CTRL_SHIFT_ESC, MOD_CONTROL | MOD_SHIFT, VK_ESCAPE);
    if (!Session->TaskManHotkey)
        WARN("WL: Unable to register Ctrl+Shift+Esc hotkey\n");

    /* Register Win+L "Lock Workstation" hotkey (optional) */
    Session->LockWkStaHotkey = RegisterHotKey(hwndSAS, IDHK_WIN_L, MOD_WIN, 'L');
    if (!Session->LockWkStaHotkey)
        WARN("WL: Unable to register Win+L hotkey\n");

    /* Register Win+U "Accessibility Utility" hotkey (optional) */
    Session->UtilManHotkey = RegisterHotKey(hwndSAS, IDHK_WIN_U, MOD_WIN, 'U');
    if (!Session->UtilManHotkey)
        WARN("WL: Unable to register Win+U hotkey\n");

    return TRUE;
}

static
BOOL
UnregisterHotKeys(
    IN PWLSESSION Session,
    IN HWND hwndSAS)
{
    /* Unregister the hotkeys */
    UnregisterHotKey(hwndSAS, IDHK_CTRL_ALT_DEL);

    if (Session->TaskManHotkey)
        UnregisterHotKey(hwndSAS, IDHK_CTRL_SHIFT_ESC);

    if (Session->LockWkStaHotkey)
        UnregisterHotKey(hwndSAS, IDHK_WIN_L);

    if (Session->UtilManHotkey)
        UnregisterHotKey(hwndSAS, IDHK_WIN_U);

    return TRUE;
}

static
BOOL
HandleMessageBeep(
    _In_ PWLSESSION Session,
    _In_ UINT uType)
{
    LPWSTR EventName;

    switch (uType)
    {
        case 0xFFFFFFFF:
            EventName = NULL;
            break;
        case MB_OK:
            EventName = L"SystemDefault";
            break;
        case MB_ICONASTERISK:
            EventName = L"SystemAsterisk";
            break;
        case MB_ICONEXCLAMATION:
            EventName = L"SystemExclamation";
            break;
        case MB_ICONHAND:
            EventName = L"SystemHand";
            break;
        case MB_ICONQUESTION:
            EventName = L"SystemQuestion";
            break;
        default:
            WARN("Unhandled type %d\n", uType);
            EventName = L"SystemDefault";
    }

    return PlayEventSound(Session, EventName);
}

static
LRESULT
CALLBACK
SASWindowProc(
    IN HWND hwndDlg,
    IN UINT uMsg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    PWLSESSION Session = (PWLSESSION)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_HOTKEY:
        {
            switch (wParam)
            {
                case IDHK_CTRL_ALT_DEL:
                {
                    TRACE("SAS: CONTROL+ALT+DELETE\n");
                    if (!Session->Gina.UseCtrlAltDelete)
                        break;
                    PostMessageW(Session->SASWindow, WLX_WM_SAS, WLX_SAS_TYPE_CTRL_ALT_DEL, 0);
                    return TRUE;
                }
                case IDHK_CTRL_SHIFT_ESC:
                {
                    TRACE("SAS: CONTROL+SHIFT+ESCAPE\n");
                    DoGenericAction(Session, WLX_SAS_ACTION_TASKLIST);
                    return TRUE;
                }
                case IDHK_WIN_L:
                {
                    TRACE("SAS: WIN+L\n");
                    PostMessageW(Session->SASWindow, WM_LOGONNOTIFY, LN_LOCK_WORKSTATION, 0);
                    return TRUE;
                }
                case IDHK_WIN_U:
                {
                    TRACE("SAS: WIN+U\n");
                    // PostMessageW(Session->SASWindow, WM_LOGONNOTIFY, LN_ACCESSIBILITY, 0);
                    return TRUE;
                }
            }
            break;
        }
        case WM_CREATE:
        {
            /* Get the session pointer from the create data */
            Session = (PWLSESSION)((LPCREATESTRUCT)lParam)->lpCreateParams;

            /* Save the Session pointer */
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)Session);
            if (GetSetupType())
                return TRUE;
            return RegisterHotKeys(Session, hwndDlg);
        }
        case WM_DESTROY:
        {
            if (!GetSetupType())
                UnregisterHotKeys(Session, hwndDlg);
            PostQuitMessage(0);
            return TRUE;
        }
        case WM_SETTINGCHANGE:
        {
            UINT uiAction = (UINT)wParam;
            if (uiAction == SPI_SETSCREENSAVETIMEOUT
             || uiAction == SPI_SETSCREENSAVEACTIVE)
            {
                SetEvent(Session->hScreenSaverParametersChanged);
            }
            return TRUE;
        }
        case WM_LOGONNOTIFY:
        {
            switch(wParam)
            {
                case LN_MESSAGE_BEEP:
                {
                    return HandleMessageBeep(Session, lParam);
                }
                case LN_SHELL_EXITED:
                {
                    /* lParam is the exit code */
                    if (lParam != 1 &&
                        Session->LogonState != STATE_LOGGED_OFF &&
                        Session->LogonState != STATE_LOGGED_OFF_SAS)
                    {
                        SetTimer(hwndDlg, 1, 1000, NULL);
                    }
                    break;
                }
                case LN_START_SCREENSAVE:
                {
                    DispatchSAS(Session, WLX_SAS_TYPE_SCRNSVR_TIMEOUT);
                    break;
                }
#if 0
                case LN_ACCESSIBILITY:
                {
                    ERR("LN_ACCESSIBILITY(lParam = %lu)\n", lParam);
                    break;
                }
#endif
                case LN_LOCK_WORKSTATION:
                {
                    DoGenericAction(Session, WLX_SAS_ACTION_LOCK_WKSTA);
                    break;
                }
                case LN_LOGOFF:
                {
                    UINT Flags = (UINT)lParam;
                    UINT Action = Flags & EWX_ACTION_MASK;
                    DWORD wlxAction;

                    TRACE("\tFlags : 0x%lx\n", lParam);

                    /*
                     * Our caller (USERSRV) should have added the shutdown flag
                     * when setting also poweroff or reboot.
                     */
                    if ((Action & (EWX_POWEROFF | EWX_REBOOT)) && !(Action & EWX_SHUTDOWN))
                    {
                        ERR("Missing EWX_SHUTDOWN flag for poweroff or reboot; action 0x%x\n", Action);
                        return STATUS_INVALID_PARAMETER;
                    }

                    // INVESTIGATE: Our HandleLogoff/HandleShutdown may instead
                    // take an EWX_* flags combination to determine what to do
                    // more precisely.
                    /* Map EWX_* flags to WLX_* actions and check for any unhandled flag */
                    if (Action & EWX_POWEROFF)
                    {
                        wlxAction = WLX_SAS_ACTION_SHUTDOWN_POWER_OFF;
                        Action &= ~(EWX_SHUTDOWN | EWX_POWEROFF);
                    }
                    else if (Action & EWX_REBOOT)
                    {
                        wlxAction = WLX_SAS_ACTION_SHUTDOWN_REBOOT;
                        Action &= ~(EWX_SHUTDOWN | EWX_REBOOT);
                    }
                    else if (Action & EWX_SHUTDOWN)
                    {
                        wlxAction = WLX_SAS_ACTION_SHUTDOWN;
                        Action &= ~EWX_SHUTDOWN;
                    }
                    else // EWX_LOGOFF
                    {
                        if (Action & EWX_FORCE)
                            wlxAction = WLX_SAS_ACTION_FORCE_LOGOFF;
                        else
                            wlxAction = WLX_SAS_ACTION_LOGOFF;
                        Action &= ~(EWX_LOGOFF | EWX_FORCE);
                    }
                    if (Action)
                        ERR("Unhandled EWX_* action flags: 0x%x\n", Action);

                    TRACE("In LN_LOGOFF, exit_in_progress == %s\n",
                        ExitReactOSInProgress ? "TRUE" : "FALSE");

                    /*
                     * In case a parallel shutdown request is done (while we are
                     * being to shut down) and it was not done by Winlogon itself,
                     * then just stop here.
                     */
#if 0
// This code is commented at the moment (even if it's correct) because
// our log-offs do not really work: the shell is restarted, no app is killed
// etc... and as a result you just get explorer opening "My Documents". And
// if you try now a shut down, it won't work because winlogon thinks it is
// still in the middle of a shutdown.
// Maybe we also need to reset ExitReactOSInProgress somewhere else??
                    if (ExitReactOSInProgress && (lParam & EWX_CALLER_WINLOGON) == 0)
                    {
                        break;
                    }
#endif
                    /* Now do the shutdown action proper */
                    DoGenericAction(Session, wlxAction);
                    return 1;
                }
                case LN_LOGOFF_CANCELED:
                {
                    ERR("Logoff canceled! Before: exit_in_progress == %s; After: FALSE\n",
                        ExitReactOSInProgress ? "TRUE" : "FALSE");

                    ExitReactOSInProgress = FALSE;
                    return 1;
                }
                default:
                {
                    ERR("WM_LOGONNOTIFY case %d is unimplemented\n", wParam);
                }
            }
            return 0;
        }
        case WM_TIMER:
        {
            if (wParam == 1)
            {
                KillTimer(hwndDlg, 1);
                StartUserShell(Session);
            }
            break;
        }
        case WLX_WM_SAS:
        {
            DispatchSAS(Session, (DWORD)wParam);
            return TRUE;
        }
    }

    return DefWindowProc(hwndDlg, uMsg, wParam, lParam);
}

BOOL
InitializeSAS(
    IN OUT PWLSESSION Session)
{
    WNDCLASSEXW swc;
    BOOL ret = FALSE;

    if (!SwitchDesktop(Session->WinlogonDesktop))
    {
        ERR("WL: Failed to switch to winlogon desktop\n");
        goto cleanup;
    }

    /* Register SAS window class */
    swc.cbSize = sizeof(WNDCLASSEXW);
    swc.style = CS_SAVEBITS;
    swc.lpfnWndProc = SASWindowProc;
    swc.cbClsExtra = 0;
    swc.cbWndExtra = 0;
    swc.hInstance = hAppInstance;
    swc.hIcon = NULL;
    swc.hCursor = NULL;
    swc.hbrBackground = NULL;
    swc.lpszMenuName = NULL;
    swc.lpszClassName = WINLOGON_SAS_CLASS;
    swc.hIconSm = NULL;
    if (RegisterClassExW(&swc) == 0)
    {
        ERR("WL: Failed to register SAS window class\n");
        goto cleanup;
    }

    /* Create invisible SAS window */
    Session->SASWindow = CreateWindowExW(
        0,
        WINLOGON_SAS_CLASS,
        WINLOGON_SAS_TITLE,
        WS_POPUP,
        0, 0, 0, 0, 0, 0,
        hAppInstance, Session);
    if (!Session->SASWindow)
    {
        ERR("WL: Failed to create SAS window\n");
        goto cleanup;
    }

    /* Register SAS window to receive SAS notifications */
    if (!SetLogonNotifyWindow(Session->SASWindow))
    {
        ERR("WL: Failed to register SAS window\n");
        goto cleanup;
    }

    if (!SetDefaultLanguage(NULL))
        return FALSE;

    ret = TRUE;

cleanup:
    if (!ret)
        UninitializeSAS(Session);
    return ret;
}
