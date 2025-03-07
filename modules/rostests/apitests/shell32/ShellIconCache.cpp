/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for the system image list
 * COPYRIGHT:   Copyright 2024 Whindmar Saksit <whindsaks@proton.me>
 */

#include "shelltest.h"
#include <versionhelpers.h>
#include <commoncontrols.h> // IID_IImageList
#include <shellutils.h>
enum { siid_docnoassoc = 0, siid_folder = 3 };

#define UNIQUEEXT L"ABC123XYZ"
#define init_(x, msg) ( (x) ? TRUE : (skip("Init %s\n", msg), FALSE) )
#define init(x) init_((x), "")

template<class T> ULONG SafeReleaseAndZero(T *&p)
{
    ULONG ret;
    if (p)
        ret = p->Release();
    p = NULL;
    return ret;
}

static HRESULT BindToParentFolder(LPCITEMIDLIST pidl, IShellFolder **ppSF, PCUITEMID_CHILD *ppChild = NULL)
{
    return SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, ppSF), ppChild);
}

enum {
    SILINIT_SHMapPIDLToSystemImageListIndex,
    SILINIT_Shell_GetCachedImageIndex,
    SILINIT_SHGetFileInfo,
    SILINIT_SHGetNoAssocIconIndex,
    SILINIT_FileIconInit_FALSE,
    SILINIT_FileIconInit_TRUE,
    SILINITCOUNT,
};

static int ChildProcessTest(UINT TestId, PWSTR GetName)
{
    CCoInit ComInit;
    HRESULT hr;
    WCHAR pathNoAssoc[MAX_PATH], pathDllFile[MAX_PATH];
    GetTempPathW(_countof(pathNoAssoc), pathNoAssoc);
    PathAppendW(pathNoAssoc, L"file." UNIQUEEXT);
    GetTempPathW(_countof(pathDllFile), pathDllFile);
    PathAppendW(pathDllFile, L"file.dll");
    CComHeapPtr<ITEMIDLIST> pidlNoAssoc(SHSimpleIDListFromPath(pathNoAssoc));
    CComHeapPtr<ITEMIDLIST> pidlDllFile(SHSimpleIDListFromPath(pathDllFile));
    int success = FALSE, skipped = FALSE, IndexNoAssoc = -1, IndexTest;

    switch (TestId)
    {
        case SILINIT_SHMapPIDLToSystemImageListIndex:
        {
            if (GetName)
                return wsprintfW(GetName, L"%hs init SIL", "SHMapPIDLToSystemImageListIndex");
            CComPtr<IShellFolder> pSF;
            PCUITEMID_CHILD pidlChild;
            if (SUCCEEDED(BindToParentFolder(pidlNoAssoc, &pSF, &pidlChild)))
                IndexNoAssoc = SHMapPIDLToSystemImageListIndex(pSF, pidlChild, NULL);
            pSF = NULL;
            if (init(SUCCEEDED(hr = BindToParentFolder(pidlDllFile, &pSF, &pidlChild))))
            {
                IndexTest = SHMapPIDLToSystemImageListIndex(pSF, pidlChild, NULL);
                success = IndexTest >= 0 && IndexTest > IndexNoAssoc;
            }
            break;
        }
        case SILINIT_Shell_GetCachedImageIndex:
        {
            if (GetName)
                return wsprintfW(GetName, L"%hs init SIL", "Shell_GetCachedImageIndex");
            IndexNoAssoc = Shell_GetCachedImageIndex(L"shell32.dll", siid_docnoassoc, 0);
            IndexTest = Shell_GetCachedImageIndex(L"shell32.dll", siid_folder, 0);
            success = IndexTest >= 0 && IndexTest > IndexNoAssoc;
            break;
        }
        case SILINIT_SHGetFileInfo:
        {
            if (GetName)
                return wsprintfW(GetName, L"%hs init SIL", "SHGetFileInfo");
            SHFILEINFO fi;
            UINT flags = SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SHELLICONSIZE;
            IndexNoAssoc = SHGetFileInfoW((PWSTR)(ITEMIDLIST*)pidlNoAssoc, 0, &fi, sizeof(fi), flags) ? fi.iIcon : -1;
            IndexTest = SHGetFileInfoW((PWSTR)(ITEMIDLIST*)pidlDllFile, 0, &fi, sizeof(fi), flags) ? fi.iIcon : -1;
            success = IndexTest >= 0 && IndexTest > IndexNoAssoc;
            break;
        }
        case SILINIT_SHGetNoAssocIconIndex:
        {
            if (GetName)
                return wsprintfW(GetName, L"%hs init SIL", "SHGetNoAssocIconIndex");
            int (WINAPI*SHGNAII)();
            (FARPROC&)SHGNAII = GetProcAddress(GetModuleHandleA("SHELL32"), (char*)848);
            if (!SHGNAII && ++skipped)
                break;
            IndexTest = SHGNAII(); // Do this first since this is the actual test
            IndexNoAssoc = Shell_GetCachedImageIndex(L"shell32.dll", siid_docnoassoc, 0);
            success = IndexTest == 0 && IndexTest == IndexNoAssoc;
            break;
        }
        case SILINIT_FileIconInit_FALSE:
        case SILINIT_FileIconInit_TRUE:
        {
            BOOL FullInit = TestId != SILINIT_FileIconInit_FALSE;
            if (GetName)
                return wsprintfW(GetName, L"%hs(%d) init SIL", "FileIconInit", FullInit);
            FileIconInit(FullInit);
            HIMAGELIST hILL = NULL, hILS = NULL;
            UINT count = Shell_GetImageLists(&hILL, &hILS) ? ImageList_GetImageCount(hILL) : 0;
            success = count >= (FullInit ? 30 : 3); // NT5=3 NT6,10=4. 30 is just a guess.
            break;
        }
    }
    return skipped ? 1337 : success ? 42 : 1;
}

START_TEST(ShellIconCache)
{
    PCSTR TestName = "ShellIconCache";
    const UINT WinMaj = LOBYTE(GetVersion());
    WCHAR buf[MAX_PATH], buf2[MAX_PATH];

    // We have to use a child process to test FileIconInit callers because
    // the system image list can only be initialized once per process.
    *buf = UNICODE_NULL;
    GetEnvironmentVariableW(L"ROSTEST_SHELL32_SIL", buf, _countof(buf));
    if (UINT fork = StrToIntW(buf))
        ExitProcess(ChildProcessTest(fork - 1, NULL));

    CCoInit ComInit;
    GetModuleFileNameW(NULL, buf, _countof(buf));
    for (UINT i = 0; i < SILINITCOUNT; ++i)
    {
        wsprintfW(buf2, L"%d", i + 1);
        if (!init(SetEnvironmentVariableW(L"ROSTEST_SHELL32_SIL", buf2)))
            continue;
        wsprintfW(buf2, L" %hs", TestName);
        SHELLEXECUTEINFOW sei = { sizeof(sei), SEE_MASK_NOCLOSEPROCESS };
        sei.lpFile = buf;
        sei.lpParameters = buf2;
        if (init(ShellExecuteExW(&sei)))
        {
            WaitForSingleObject(sei.hProcess, INFINITE);
            DWORD exitcode = -1;
            GetExitCodeProcess(sei.hProcess, &exitcode);
            CloseHandle(sei.hProcess);
            ChildProcessTest(i, buf2);
            if (exitcode != 1337)
                ok(exitcode == 42, "%ls\n", buf2);
            else
                skip("%ls\n", buf2);
        }
    }

    HIMAGELIST hIL;
    ok(Shell_GetImageLists(NULL, &hIL) != FALSE, "Shell_GetImageLists failed\n");
    ok(Shell_GetImageLists(&hIL, NULL) != FALSE, "Shell_GetImageLists failed\n");
    IUnknown *pUnk = NULL;
    ok(FAILED(SHGetImageList(SHIL_LARGE, IID_IShellLink, (void**)&pUnk)), "SHGetImageList must fail on wrong IID\n");
    SafeReleaseAndZero(pUnk);
    ok(FAILED(SHGetImageList(0xbaad, IID_IImageList, (void**)&pUnk)), "SHGetImageList must fail on wrong SHIL\n");
    SafeReleaseAndZero(pUnk);

    UINT count = 0, expected = WinMaj == 5 || IsReactOS() ? 4 : 5;
    C_ASSERT(SHIL_LARGE == 0);
    for (; SUCCEEDED(SHGetImageList(count, IID_IImageList, (void**)&pUnk)) && count < expected * 10;)
        count += SafeReleaseAndZero(pUnk) != 0;
    // Note: Windows 10 has one extra undocumented list that we don't test for.
    ok(count >= expected, "Must have at least %d lists, got %d\n", expected, count);
}
