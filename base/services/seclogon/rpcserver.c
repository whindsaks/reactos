/*
 * PROJECT:     ReactOS Secondary Logon Service
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Secondary Logon service RPC server
 * COPYRIGHT:   Eric Kohl 2022 <eric.kohl@reactos.org>
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"

#include <seclogon_s.h>
#include <ntsecapi.h>

WINE_DEFAULT_DEBUG_CHANNEL(seclogon);

HANDLE g_hLSA = NULL;
#define SE_TCB_PRIVILEGE                  (7L) /* TODO: call regular advapi func */
#define NewCredentials 9 //TODO: fix sdk/include/psdk/ntsecapi.h

/* FUNCTIONS *****************************************************************/



void __RPC_FAR * __RPC_USER midl_user_allocate(SIZE_T len)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
}


void __RPC_USER midl_user_free(void __RPC_FAR * ptr)
{
    HeapFree(GetProcessHeap(), 0, ptr);
}

static
void*
GetTokenInformationAlloc(HANDLE hToken, TOKEN_INFORMATION_CLASS Class, PDWORD pSize)
{
    void *r;
    DWORD cb = 0;
    GetTokenInformation(hToken, Class, NULL, 0, &cb);
    if (!cb)
        return NULL;
    r = LocalAlloc(LPTR, cb);
    if (r)
    {
        if (!GetTokenInformation(hToken, Class, r, cb, pSize))
        {
            LocalFree(r);
            r = NULL;
        }
    }
    return r;
}

static
void*
GetCurrentThreadTokenInformationAlloc(TOKEN_INFORMATION_CLASS Class, PDWORD pSize)
{
    void *r;
    HANDLE hToken;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hToken))
        return NULL;
    r = GetTokenInformationAlloc(hToken, Class, pSize);
    CloseHandle(hToken);
    return r;
}


DWORD
StartRpcServer(VOID)
{
    UNICODE_STRING LsaName;
    NTSTATUS Status;
    ULONG SecurityMode;
    BOOLEAN HadTCB;

    if (!g_hLSA)
    {
        RtlInitUnicodeString(&LsaName, L"seclogon"); /* TODO: What is the correct name? */
        RtlAdjustPrivilege(SE_TCB_PRIVILEGE, TRUE, FALSE, &HadTCB);
        Status = LsaRegisterLogonProcess((PLSA_STRING)&LsaName, &g_hLSA, &SecurityMode); 
        RtlAdjustPrivilege(SE_TCB_PRIVILEGE, HadTCB, FALSE, &HadTCB);
    }

    Status = lpServiceGlobals->StartRpcServer(L"seclogon", ISeclogon_v1_0_s_ifspec);
    TRACE("StartRpcServer returned 0x%08lx\n", Status);

    return RtlNtStatusToDosError(Status);
}


DWORD
StopRpcServer(VOID)
{
    NTSTATUS Status;

    Status = lpServiceGlobals->StopRpcServer(ISeclogon_v1_0_s_ifspec);
    TRACE("StopRpcServer returned 0x%08lx\n", Status);

    if (g_hLSA)
    {
        LsaDeregisterLogonProcess(g_hLSA);
        g_hLSA = NULL;
    }

    return RtlNtStatusToDosError(Status);
}

static
BOOL
LogonUserHelper(
_In_ LPCWSTR lpszUsername,
_In_opt_ LPCWSTR lpszDomain,
_In_opt_ LPCWSTR lpszPassword,
_In_ DWORD dwLogonType,
_In_ DWORD dwLogonProvider, /* TODO: How do we map this to LsaLogonUser? */
_Out_ PHANDLE phToken,
_In_opt_ PSID InjectLogonSid,
_Out_ LPWSTR *pProfilePath)
{
    LSA_STRING OriginName, AuthPackageName;
    LPCSTR AuthPackageNamePtr;
    void *ProfileBuffer;
    SIZE_T cbAP;
    ULONG AuthPackage, ProfileBufferLength;
    NTSTATUS Status, SubStatus;
    SECURITY_LOGON_TYPE slt = Interactive;
    BOOL classic = TRUE;
    UINT MessageType;
    MSV1_0_INTERACTIVE_LOGON *msv10;
    BYTE tgbuffer[FIELD_OFFSET(TOKEN_GROUPS, Groups[2])];
    TOKEN_GROUPS *pTG = (TOKEN_GROUPS *)tgbuffer;
    TOKEN_SOURCE ts = { 0 };
    LUID LogonId;
    QUOTA_LIMITS Quotas;

    if (!lpszUsername)
        lpszUsername = L"";
    if (!lpszDomain)
        lpszDomain = L"";
    if (!lpszPassword)
        lpszPassword = L"";

    /* User@Domain or classic? */
    if (!lpszDomain[0])
    {
        SIZE_T i, at = 0;
        for (i = 0; lpszUsername[i]; ++i)
        {
            if (lpszUsername[i] == L'@')
                at = i;
        }
        classic = !at;
    }

    if (classic)
    {
        AuthPackageNamePtr = MSV1_0_PACKAGE_NAME;
        MessageType = MsV1_0InteractiveLogon;
OutputDebugStringA("MsV1_0InteractiveLogon\n");
    }
    else
    {
        C_ASSERT(sizeof(KERB_INTERACTIVE_LOGON) == sizeof(MSV1_0_INTERACTIVE_LOGON));
        AuthPackageNamePtr = "Negotiate";
        MessageType = KerbInteractiveLogon;
OutputDebugStringA("KerbInteractiveLogon\n");
    }

    RtlInitAnsiString(&AuthPackageName, AuthPackageNamePtr);
    Status = LsaLookupAuthenticationPackage(g_hLSA, &AuthPackageName, &AuthPackage);
{char b[99];sprintf(b,"LsaLookupAuthenticationPackage %#x\n", (UINT)Status),OutputDebugStringA(b);}
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    /* MSDN claims the entire package must be in a single buffer! */
    {
        char *p;
        SIZE_T cbD = (lstrlenW(lpszDomain) + 1) * sizeof(WCHAR);
        SIZE_T cbU = (lstrlenW(lpszUsername) + 1) * sizeof(WCHAR);
        SIZE_T cbP = (lstrlenW(lpszPassword) + 1) * sizeof(WCHAR);
        cbAP = sizeof(MSV1_0_INTERACTIVE_LOGON) + cbD + cbU + cbP;
        msv10 = (MSV1_0_INTERACTIVE_LOGON*) (p = LocalAlloc(LPTR, cbAP));
        if (!msv10)
            return FALSE;
        msv10->MessageType = MessageType;
        p += sizeof(MSV1_0_INTERACTIVE_LOGON);
        lstrcpyW((LPWSTR)p, lpszDomain);
        RtlInitUnicodeString(&msv10->LogonDomainName, (LPWSTR)p);
        p += cbD;
        lstrcpyW((LPWSTR)p, lpszUsername);
        RtlInitUnicodeString(&msv10->UserName, (LPWSTR)p);
        p += cbU;
        lstrcpyW((LPWSTR)p, lpszPassword);
        RtlInitUnicodeString(&msv10->Password, (LPWSTR)p);
        p += cbU;
    }

    if (InjectLogonSid)
    {
        pTG->GroupCount = 1;
        pTG->Groups[0].Attributes = SE_GROUP_MANDATORY | SE_GROUP_ENABLED | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_LOGON_ID;
        pTG->Groups[0].Sid = InjectLogonSid;
        //FIXME TODO todo the local thing SECURITY_LOCAL_RID
    }

    if (dwLogonType == LOGON32_LOGON_NEW_CREDENTIALS)
        slt = NewCredentials;

    C_ASSERT(sizeof("seclogon")-1 == TOKEN_SOURCE_LENGTH);
    CopyMemory(ts.SourceName, "seclogon", sizeof("seclogon")-1);
    RtlInitAnsiString(&OriginName, "seclogon"); /* TODO: What is the correct name? */
    Status = LsaLogonUser(g_hLSA, &OriginName, slt, AuthPackage,
                          msv10, cbAP, InjectLogonSid ? pTG : NULL, &ts,
                          &ProfileBuffer, &ProfileBufferLength, &LogonId,
                          phToken, &Quotas, &SubStatus);
{char b[99];sprintf(b,"LsaLogonUser %#x\n", (UINT)Status),OutputDebugStringA(b);}
    LocalFree(msv10);
    if (pProfilePath)
        *pProfilePath = NULL;
    if (NT_SUCCESS(Status) && ProfileBuffer && ProfileBufferLength)
    {
        MSV1_0_INTERACTIVE_PROFILE *msv10p = (MSV1_0_INTERACTIVE_PROFILE *) ProfileBuffer;
        if (pProfilePath && msv10p->ProfilePath.Buffer && msv10p->ProfilePath.Length)
        {
            *pProfilePath = LocalAlloc(LPTR, (msv10p->ProfilePath.Length + 1) * 2);
            if (*pProfilePath)
                lstrcpyW(*pProfilePath, msv10p->ProfilePath.Buffer);
            else
                Status = STATUS_NO_MEMORY;
        }
        LsaFreeReturnBuffer(ProfileBuffer);
    }
    SetLastError(RtlNtStatusToDosError(Status));
    return NT_SUCCESS(Status);
}

VOID
__stdcall
SeclCreateProcessWithLogonW(
    _In_ handle_t hBinding,
    _In_ SECL_REQUEST *pRequest,
    _Out_ SECL_RESPONSE *pResponse)
{
    STARTUPINFOW Startup;
    PROCESS_INFORMATION ProcessInfo;
    SECURITY_ATTRIBUTES sa;
    PROFILEINFOW ProfileInfo;
    HANDLE hToken = NULL;
    HANDLE hClientProcess = NULL;
    handle_t hImpRpc = NULL;
    DWORD ClientSessionId;
    BYTE CallerLogonSidBuffer[SECURITY_MAX_SID_SIZE];
    PSID CallerLogonSid = NULL;
    LPWSTR ProfilePath = NULL, Username;
    ULONG dwError = ERROR_SUCCESS, CreationFlags;
    BOOL rc, IsImp = FALSE;
WCHAR dbgdesk[100];
    LPVOID Environment = NULL, FreeEnvironment = NULL;
    WCHAR UsernameBuf[MAX_PATH];

    TRACE("SeclCreateProcessWithLogonW(%p %p %p)\n", hBinding, pRequest, pResponse);

    if (pRequest != NULL)
    {
        TRACE("Username: '%S'\n", pRequest->Username);
        TRACE("Domain: '%S'\n", pRequest->Domain);
        TRACE("Password: '%S'\n", pRequest->Password);
        TRACE("ApplicationName: '%S'\n", pRequest->ApplicationName);
        TRACE("CommandLine: '%S'\n", pRequest->CommandLine);
        TRACE("CurrentDirectory: '%S'\n", pRequest->CurrentDirectory);
        TRACE("LogonFlags: 0x%lx\n", pRequest->dwLogonFlags);
        TRACE("CreationFlags: 0x%lx\n", pRequest->dwCreationFlags);
        TRACE("ProcessId: %lu\n", pRequest->dwProcessId);
    }


    hImpRpc = (dwError = RpcImpersonateClient(hBinding)) ? NULL : hBinding;

    hClientProcess = OpenProcess(PROCESS_DUP_HANDLE,
                                       FALSE,
                                       pRequest->dwProcessId);
    if (hClientProcess == NULL)
    {
        dwError = GetLastError();
        WARN("OpenProcess() failed with Error %lu\n", dwError);
        goto done;
    }
    if (pRequest->dwLogonFlags & 0x80000000)
    {
        DWORD size, i;
        TOKEN_GROUPS *tg = GetCurrentThreadTokenInformationAlloc(TokenGroups, &size);
        if (!tg)
        {
            dwError = GetLastError();
            WARN("TokenGroups failed with Error %lu\n", dwError);
            goto done;
        }
        for (i = 0; i < tg->GroupCount; ++i)
        {
            if (tg->Groups[i].Attributes & SE_GROUP_LOGON_ID)
            {
                size = RtlLengthSid(tg->Groups[i].Sid);
                if (size <= sizeof(CallerLogonSidBuffer))
                {
                    CopyMemory(CallerLogonSidBuffer, tg->Groups[i].Sid, size);
                    CallerLogonSid = CallerLogonSidBuffer;
                }
                break;
            }
        }
        LocalFree(tg);
        if (!CallerLogonSid)
        {
            dwError = GetLastError();
            WARN("Did not find callers logon sid\n");
            goto done;
        }OutputDebugStringA("got CallerLogonSid\n");
    }
    if (!ProcessIdToSessionId(pRequest->dwProcessId, &ClientSessionId))
    {
        dwError = GetLastError();
        WARN("ProcessIdToSessionId() failed with Error %lu\n", dwError);
        goto done;
    }OutputDebugStringA("got ClientSessionId\n");

    ZeroMemory(&ProfileInfo, sizeof(ProfileInfo));

    /* Logon */
    if (!pRequest->Token)
    {
        BOOL net = pRequest->dwLogonFlags & LOGON_NETCREDENTIALS_ONLY;
        rc = LogonUserHelper(pRequest->Username, pRequest->Domain, pRequest->Password,
                       net ? LOGON32_LOGON_NEW_CREDENTIALS : LOGON32_LOGON_INTERACTIVE,
                       net ? LOGON32_PROVIDER_WINNT50 : LOGON32_PROVIDER_DEFAULT,
                       &hToken, CallerLogonSid, net ? NULL : &ProfilePath);
        if (rc == FALSE)
        {
{char b[99];sprintf(b,"LogonUserHelper failed gle=%d\n", (int)(GetLastError())),OutputDebugStringA(b);}
            dwError = GetLastError();
            WARN("LogonUser() failed with Error %lu\n", dwError);
            goto done;
        }
        Username = pRequest->Username; /* TODO: Strip away @...? */
        OutputDebugStringA("got LogonUserHelper\n");
    }
    else
    {
        HANDLE hSrcToken = (HANDLE)(INT_PTR) pRequest->Token;
        DWORD cch;
        /*if (hImpRpc)
        {
            RpcRevertToSelfEx(hImpRpc), hImpRpc = NULL;
        }*/
OutputDebugStringA("Calling DuplicateHandle\n");
        if (!DuplicateHandle(hClientProcess, hSrcToken, GetCurrentProcess(),
                             &hToken, MAXIMUM_ALLOWED, FALSE, 0))
        {
            dwError = GetLastError();
            WARN("DuplicateHandle() failed with Error %lu\n", dwError);
            goto done;
        }
OutputDebugStringA("Calling ImpersonateLoggedOnUser\n");
        if (!ImpersonateLoggedOnUser(hToken))
        {OutputDebugStringA("ImpersonateLoggedOnUser, hacking it\n");
            //dwError = GetLastError();
            //WARN("ImpersonateLoggedOnUser() failed with Error %lu\n", dwError);
            //goto done;
        }
        cch = MAX_PATH;
        GetUserName(UsernameBuf, &cch);
        Username = (LPWSTR)L"Administrator";//pRequest->dwLogonFlags=0;   //UsernameBuf;
        ProfilePath = NULL;
        if (!IsImp)
            RevertToSelf();
        OutputDebugStringW(Username),OutputDebugStringA("got uname from token\n");
    }

    if (hImpRpc)
    {
        RpcRevertToSelfEx(hImpRpc), hImpRpc = NULL;
    }

    /* Load the user profile */
    if (pRequest->dwLogonFlags & LOGON_WITH_PROFILE)
    {
        ProfileInfo.dwSize = sizeof(ProfileInfo);
        ProfileInfo.lpUserName = Username;
        ProfileInfo.lpProfilePath = ProfilePath;
        if (ProfilePath)OutputDebugStringW(ProfilePath),OutputDebugStringA("\n");
OutputDebugStringA("calling LoadUserProfileW\n");
        rc = LoadUserProfileW(hToken, &ProfileInfo);
        if (rc == FALSE)
        {
            dwError = GetLastError();
            WARN("LoadUserProfile() failed with Error %lu\n", dwError);
            goto done;
        }OutputDebugStringA("got LOGON_WITH_PROFILE\n");
    }else OutputDebugStringA("skip LOGON_WITH_PROFILE\n");

    /* Initialize the startup information */
    ZeroMemory(&Startup, sizeof(Startup));
    CopyMemory(&Startup, pRequest->Startup, min(sizeof(Startup), pRequest->dwStartupSize));
    Startup.cb = min(sizeof(Startup), pRequest->dwStartupSize);
{char b[99];sprintf(b,"SI dwStartupSize=%u lpDesktop=%p\n", (UINT)pRequest->dwStartupSize, Startup.lpDesktop),OutputDebugStringA(b);}
    /*if (Startup.lpDesktop)
    {
        Startup.lpDesktop = pRequest->Desktop;
        OutputDebugStringW(Startup.lpDesktop);OutputDebugStringW(L"\n");
    }*/
Startup.cb = sizeof(Startup);
lstrcpyW(dbgdesk, L"WinSta0\\Default");
Startup.lpDesktop = dbgdesk;

    CreationFlags = pRequest->dwCreationFlags;
    //CreationFlags &= ~DETACHED_PROCESS;
    CreationFlags |= CREATE_DEFAULT_ERROR_MODE | //CREATE_BREAKAWAY_FROM_JOB |
                     CREATE_NEW_PROCESS_GROUP ;//| CREATE_NEW_CONSOLE;

    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    sa.lpSecurityDescriptor = NULL;

if (hImpRpc) OutputDebugStringA("oops, hImpRpc should be NULL\n");
    SetTokenInformation(hToken, TokenSessionId, &ClientSessionId, sizeof(ClientSessionId));
    IsImp = ImpersonateLoggedOnUser(hToken);
OutputDebugStringA(IsImp ? "ImpersonateLoggedOnUser\n" : "ImpersonateLoggedOnUser FAILED\n");
if (Startup.lpDesktop) OutputDebugStringW(Startup.lpDesktop),OutputDebugStringW(L"\n");

    Environment = pRequest->Environment;
    if (!Environment)
    {
        OutputDebugStringA("will CreateEnvironmentBlock\n");
        if (CreateEnvironmentBlock(&Environment, hToken, FALSE))
        {
            FreeEnvironment = Environment;
            CreationFlags |= CREATE_UNICODE_ENVIRONMENT;
            OutputDebugStringA("OK CreateEnvironmentBlock\n");
        }
        else
        {
            {char b[99];sprintf(b,"CreateEnvironmentBlock fail %u for tok=%p\n", (UINT)GetLastError(), hToken),OutputDebugStringA(b);}
            Environment = NULL;
        }
    }

    /* Create Process */
    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));
    rc = CreateProcessAsUserW(hToken,
                              pRequest->ApplicationName,
                              pRequest->CommandLine,
                              &sa,  // lpProcessAttributes,
                              &sa,  // lpThreadAttributes,
                              FALSE, // bInheritHandles,
                              CreationFlags,
                              Environment,
                              pRequest->CurrentDirectory,
                              &Startup,
                              &ProcessInfo);
    if (rc == FALSE)
    {
        dwError = GetLastError();
        WARN("CreateProcessAsUser() failed with Error %lu\n", dwError);
        goto done;
    }

    /* Return process info to the caller */
    if (pResponse != NULL)
    {
        DuplicateHandle(GetCurrentProcess(),
                        ProcessInfo.hProcess,
                        hClientProcess,
                        (PHANDLE)&pResponse->hProcess,
                        0,
                        FALSE,
                        DUPLICATE_SAME_ACCESS);

        DuplicateHandle(GetCurrentProcess(),
                        ProcessInfo.hThread,
                        hClientProcess,
                        (PHANDLE)&pResponse->hThread,
                        0,
                        FALSE,
                        DUPLICATE_SAME_ACCESS);

        pResponse->dwProcessId = ProcessInfo.dwProcessId;
        pResponse->dwThreadId = ProcessInfo.dwThreadId;
    }

done:
    if (hClientProcess)
        CloseHandle(hClientProcess);

    if (ProcessInfo.hThread)
        CloseHandle(ProcessInfo.hThread);

    if (ProcessInfo.hProcess)
        CloseHandle(ProcessInfo.hProcess);

    if (ProfileInfo.hProfile)
        UnloadUserProfile(hToken, ProfileInfo.hProfile);

    if (ProfilePath)
        LocalFree(ProfilePath);

    if (IsImp)
        RevertToSelf();

    if (hToken)
        CloseHandle(hToken);

    if (FreeEnvironment)
        DestroyEnvironmentBlock(FreeEnvironment);

    if (hImpRpc)
        RpcRevertToSelfEx(hImpRpc), hImpRpc = NULL;

    if (pResponse != NULL)
        pResponse->dwError = dwError;
}
