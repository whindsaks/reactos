/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         LGPLv2.1+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Test for CMyComputer
 * PROGRAMMER:      Giannis Adamopoulos
 */

#include "shelltest.h"

#define NDEBUG
#include <debug.h>
#include <stdio.h>
#include <shellutils.h>
#include <versionhelpers.h>

#define TESTFILE L"shell32_test"
#define GUID1 L"{1F1B6E5F-4818-4709-8BC6-B85D5D386942}"
#define GUID2 L"{2254058E-77D7-46BC-A301-E6249D84910D}"
#define GUID3 L"{322168B3-5C6E-4756-A00F-9EAF3C88ADC6}"
#define GUID4 L"{417DDEC4-6462-4907-9F3D-27B5638A7B83}"

/*
#define GUID1 L"{0B9CB801-5998-4EFE-AA16-F47E6A77852F}"
{A7B5C776-18A2-473C-9917-4F74205C3A42}
{D2645B31-BFBD-482F-AFB6-855F4F53190E}
{619628C7-6605-4E96-AF8D-8240E092912E}
{857E9B9C-9B95-4FCF-801F-4A138FC209E7}
{27026446-6637-4442-9F36-A990D9F06A89}*/

VOID ResetPath(PWSTR pszPath, PCWSTR pszExt = NULL)
{
    if (pszExt)
        PathAddExtensionW(pszPath, pszExt);
    DWORD Attr = GetFileAttributesW(pszPath);
    if (!(Attr & FILE_ATTRIBUTE_DIRECTORY))
        DeleteFileW(pszPath);
    else if (Attr != INVALID_FILE_ATTRIBUTES)
        RemoveDirectoryW(pszPath);
}

LPITEMIDLIST _CreateDummyPidl()
{
    /* Create a tiny pidl with no contents */
    LPITEMIDLIST testpidl = (LPITEMIDLIST)SHAlloc(3 * sizeof(WORD));
    testpidl->mkid.cb = 2 * sizeof(WORD);
    *(WORD*)((char*)testpidl + (int)(2 * sizeof(WORD))) = 0;

    return testpidl;
}

VOID TestUninitialized()
{
    CComPtr<IShellFolder> psf;
    CComPtr<IEnumIDList> penum;
    CComPtr<IDropTarget> pdt;
    CComPtr<IContextMenu> pcm;
    CComPtr<IShellView> psv;
    LPITEMIDLIST retrievedPidl;
    ULONG pceltFetched;
    HRESULT hr;

    /* Create a CFSFolder */
    hr = CoCreateInstance(CLSID_ShellFSFolder, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IShellFolder, &psf));
    ok(hr == S_OK, "hr = %lx\n", hr);

    /* An uninitialized CFSFolder doesn't contain any items */
    hr = psf->EnumObjects(NULL, 0, &penum);
    ok(hr == S_OK, "hr = %lx\n", hr);
    hr = penum->Next(0, &retrievedPidl, &pceltFetched);
    ok(hr == S_FALSE, "hr = %lx\n", hr);
    hr = penum->Next(1, &retrievedPidl, &pceltFetched);
    ok(hr == S_FALSE, "hr = %lx\n", hr);

    /* It supports viewing */
    hr = psf->CreateViewObject(NULL, IID_PPV_ARG(IDropTarget, &pdt));
    ok(hr == S_OK, "hr = %lx\n", hr);
    hr = psf->CreateViewObject(NULL, IID_PPV_ARG(IContextMenu, &pcm));
    ok(hr == S_OK, "hr = %lx\n", hr);
    hr = psf->CreateViewObject(NULL, IID_PPV_ARG(IShellView, &psv));
    ok(hr == S_OK, "hr = %lx\n", hr);

    /* And its display name is ... "C:\Documents and Settings\<username>\Desktop" */
    STRRET strretName;
    hr = psf->GetDisplayNameOf(NULL,SHGDN_FORPARSING,&strretName);
    ok(hr == S_OK, "hr = %lx\n", hr);
    ok(strretName.uType == STRRET_WSTR, "strretName.uType == %x\n", strretName.uType);
    ok((wcsncmp(strretName.pOleStr, L"C:\\Documents and Settings\\", 26) == 0) ||
       (wcsncmp(strretName.pOleStr, L"C:\\Users\\", 9) == 0),
       "wrong name, got: %S\n", strretName.pOleStr);
    ok(wcscmp(strretName.pOleStr + wcslen(strretName.pOleStr) - 8, L"\\Desktop") == NULL,
       "wrong name, got: %S\n", strretName.pOleStr);

    hr = psf->GetDisplayNameOf(NULL,SHGDN_FORPARSING|SHGDN_INFOLDER,&strretName);
    ok(hr == E_INVALIDARG, "hr = %lx\n", hr);




    /* Use Initialize method with  a dummy pidl and test the still non initialized CFSFolder */
    CComPtr<IPersistFolder2> ppf2;
    hr = psf->QueryInterface(IID_PPV_ARG(IPersistFolder2, &ppf2));
    ok(hr == S_OK, "hr = %lx\n", hr);

    LPITEMIDLIST testpidl = _CreateDummyPidl();

    hr = ppf2->Initialize(testpidl);
    ok(hr == S_OK, "hr = %lx\n", hr);

    CComHeapPtr<ITEMIDLIST> pidl;
    hr = ppf2->GetCurFolder(&pidl);
    ok(hr == S_OK, "hr = %lx\n", hr);
    ok(pidl->mkid.cb == 2 * sizeof(WORD), "got wrong pidl size, cb = %x\n", pidl->mkid.cb);

    /* methods that worked before, now fail */
    hr = psf->GetDisplayNameOf(NULL,SHGDN_FORPARSING,&strretName);
    ok(hr == E_INVALIDARG || hr == E_FAIL, "hr = %lx\n", hr);
    hr = psf->EnumObjects(NULL, 0, &penum);
    ok(hr == E_INVALIDARG || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED), "hr = %lx\n", hr);

    /* The following continue to work though */
    hr = psf->CreateViewObject(NULL, IID_PPV_ARG(IDropTarget, &pdt));
    ok(hr == S_OK, "hr = %lx\n", hr);
    hr = psf->CreateViewObject(NULL, IID_PPV_ARG(IContextMenu, &pcm));
    ok(hr == S_OK, "hr = %lx\n", hr);
    hr = psf->CreateViewObject(NULL, IID_PPV_ARG(IShellView, &psv));
    ok(hr == S_OK, "hr = %lx\n", hr);

}

VOID TestInitialize()
{
    HRESULT hr;
    STRRET strretName;

    /* Create a CFSFolder */
    CComPtr<IShellFolder> psf;
    hr = CoCreateInstance(CLSID_ShellFSFolder, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IShellFolder, &psf));
    ok(hr == S_OK, "hr = %lx\n", hr);

    CComPtr<IPersistFolder3> ppf3;
    hr = psf->QueryInterface(IID_PPV_ARG(IPersistFolder3, &ppf3));
    ok(hr == S_OK, "hr = %lx\n", hr);

    LPITEMIDLIST testpidl = _CreateDummyPidl();
    PERSIST_FOLDER_TARGET_INFO pfti = {0};
    PERSIST_FOLDER_TARGET_INFO queriedPfti;
    hr = ppf3->InitializeEx(NULL, NULL, NULL);
    ok(hr == E_INVALIDARG || // Vista+
       hr == E_OUTOFMEMORY,  // Win2k3
       "hr = %lx\n", hr);

    hr = ppf3->InitializeEx(NULL, NULL, &pfti);
    ok(hr == E_INVALIDARG || // Vista+
       hr == E_OUTOFMEMORY,  // Win2k3
       "hr = %lx\n", hr);

    wcscpy(pfti.szTargetParsingName, L"C:\\");
    hr = ppf3->InitializeEx(NULL, NULL, &pfti);
    ok(hr == E_INVALIDARG || // Vista+
       hr == E_OUTOFMEMORY,  // Win2k3
       "hr = %lx\n", hr);

    hr = ppf3->InitializeEx(NULL, testpidl, NULL);
    ok(hr == S_OK, "hr = %lx\n", hr);

    hr = ppf3->GetFolderTargetInfo(&queriedPfti);
    ok(hr == E_FAIL ||       // Win7+
       hr == S_OK,           // Win2k3-Vista
       "hr = %lx\n", hr);

    hr = psf->GetDisplayNameOf(NULL,SHGDN_FORPARSING,&strretName);
    ok(hr == E_INVALIDARG || hr == E_FAIL, "hr = %lx\n", hr);

    pfti.szTargetParsingName[0] = 0;
    hr = ppf3->InitializeEx(NULL, testpidl, &pfti);
    ok(hr == S_OK, "hr = %lx\n", hr);

    hr = ppf3->GetFolderTargetInfo(&queriedPfti);
    ok(hr == S_OK, "hr = %lx\n", hr);
    ok(wcscmp(queriedPfti.szTargetParsingName, L"") == 0, "wrong name, got: %S\n", queriedPfti.szTargetParsingName);

    hr = psf->GetDisplayNameOf(NULL,SHGDN_FORPARSING,&strretName);
    ok(hr == E_INVALIDARG || hr == E_FAIL, "hr = %lx\n", hr);

    wcscpy(pfti.szTargetParsingName, L"C:\\");
    hr = ppf3->InitializeEx(NULL, testpidl, &pfti);
    ok(hr == S_OK, "hr = %lx\n", hr);

    hr = ppf3->GetFolderTargetInfo(&queriedPfti);
    ok(hr == S_OK, "hr = %lx\n", hr);
    ok(wcscmp(queriedPfti.szTargetParsingName, L"C:\\") == 0, "wrong name, got: %S\n", queriedPfti.szTargetParsingName);

    hr = psf->GetDisplayNameOf(NULL,SHGDN_FORPARSING,&strretName);
    ok(hr == S_OK, "hr = %lx\n", hr);
    ok(strretName.uType == STRRET_WSTR, "strretName.uType == %x\n", strretName.uType);
    ok(wcscmp(strretName.pOleStr, L"C:\\") == 0, "wrong name, got: %S\n", strretName.pOleStr);
}

VOID TestGetUIObjectOf()
{
    HRESULT hr;

    /* Create a CFSFolder */
    CComPtr<IShellFolder> psf;
    hr = CoCreateInstance(CLSID_ShellFSFolder, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IShellFolder, &psf));
    ok(hr == S_OK, "hr = %lx\n", hr);

    /* test 0 cidl for IDataObject */
    CComPtr<IDataObject> pdo;
    hr = psf->GetUIObjectOf(NULL, 0, NULL, IID_NULL_PPV_ARG(IDataObject, &pdo));
    ok(hr == E_INVALIDARG, "hr = %lx\n", hr);
}

VOID TestGetDisplayNameOf()
{
    HRESULT hr;
    WCHAR szPath[MAX_PATH + 42], szBuf[MAX_PATH];
    DWORD cch = GetTempPathW(MAX_PATH, szPath);
    if (!cch || cch > MAX_PATH)
    {
        skip("Unable to initialize\n");
        return;
    }
    PathAppendW(szPath, TESTFILE);
    cch = lstrlenW(szPath);

    const bool OrgHideExt = SHELL_GetSetting(SSF_SHOWEXTENSIONS, fShowExtensions) == FALSE; // (Inverted)
    const bool OrgSuperHidden = SHELL_GetSetting(SSF_SHOWSUPERHIDDEN, fShowSuperHidden) != FALSE;
    RegSetDWORD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"HideFileExt", TRUE);
    RegSetDWORD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"ShowSuperHidden", TRUE);
    SHGetSetSettings(NULL, 0, TRUE); // Invalidate SHELLSTATE cache

    // Note: Windows caches ProgId/Class info so each check needs a unique GUID
    szPath[cch] = UNICODE_NULL;
    ResetPath(szPath, L"." GUID1);
    CreateDirectoryW(szPath, NULL);
    hr = GetDisplayNameOf(szPath, SHGDN_INFOLDER, szBuf, _countof(szBuf));
    ok(hr == S_OK && !lstrcmpiW(szBuf, TESTFILE L"." GUID1), "Folder junction-extension visible with ShowSuperHidden\n");
    ResetPath(szPath);

    szPath[cch] = UNICODE_NULL;
    ResetPath(szPath, L"." GUID2);
    CloseHandle(CreateFileW(szPath, 0, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
    hr = GetDisplayNameOf(szPath, SHGDN_INFOLDER, szBuf, _countof(szBuf));
    ok(hr == S_OK && !lstrcmpiW(szBuf, TESTFILE L"." GUID2), "File junction-extension visible with ShowSuperHidden\n");
    ResetPath(szPath);

    RegSetDWORD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"ShowSuperHidden", FALSE);
    SHGetSetSettings(NULL, 0, TRUE); // Invalidate SHELLSTATE cache

    szPath[cch] = UNICODE_NULL;
    ResetPath(szPath, L"." GUID3);
    CreateDirectoryW(szPath, NULL);
    RegSetDWORD(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" GUID3, NULL, 0); // The class key needs to exist on NT6
    hr = GetDisplayNameOf(szPath, SHGDN_INFOLDER, szBuf, _countof(szBuf));
    ok(hr == S_OK && !lstrcmpiW(szBuf, TESTFILE), "Folder junction-extension\n");
    // TODO: NoFileFolderJunction value will force the extension on?
    SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" GUID3);
    ResetPath(szPath);

    szPath[cch] = UNICODE_NULL;
    ResetPath(szPath, L"." GUID4);
    CloseHandle(CreateFileW(szPath, 0, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
    hr = GetDisplayNameOf(szPath, SHGDN_INFOLDER, szBuf, _countof(szBuf));
    ok(hr == S_OK && !lstrcmpiW(szBuf, TESTFILE L"." GUID4), "File junction-extension always visible?\n");
    ResetPath(szPath);

    szPath[cch] = UNICODE_NULL;
    ResetPath(szPath, L".lnk");
    CloseHandle(CreateFileW(szPath, 0, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
    hr = GetDisplayNameOf(szPath, SHGDN_INFOLDER, szBuf, _countof(szBuf));
    ok(hr == S_OK && !lstrcmpiW(szBuf, TESTFILE), "File with NeverShowExt extension\n");
    DeleteFileW(szPath);
    CreateDirectoryW(szPath, NULL);
    hr = GetDisplayNameOf(szPath, SHGDN_INFOLDER, szBuf, _countof(szBuf));
    ok(hr == S_OK && !lstrcmpiW(szBuf, TESTFILE L".lnk"), "Folder with NeverShowExt extension\n");
    ResetPath(szPath);

    szPath[cch] = UNICODE_NULL;
    ResetPath(szPath, L".NotRegistered");
    CloseHandle(CreateFileW(szPath, 0, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
    hr = GetDisplayNameOf(szPath, SHGDN_INFOLDER, szBuf, _countof(szBuf));
    ok(hr == S_OK && !lstrcmpiW(szBuf, TESTFILE L".NotRegistered"), "File with unknown extension\n");
    ResetPath(szPath);

    szPath[cch] = UNICODE_NULL;
    ResetPath(szPath, L".exe");
    CloseHandle(CreateFileW(szPath, 0, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
    hr = GetDisplayNameOf(szPath, SHGDN_INFOLDER, szBuf, _countof(szBuf));
    ok(hr == S_OK && !lstrcmpiW(szBuf, TESTFILE), "File with known extension %#x\n", SHGDN_INFOLDER);
    hr = GetDisplayNameOf(szPath, SHGDN_INFOLDER | SHGDN_FORPARSING, szBuf, _countof(szBuf));
    ok(hr == S_OK && !lstrcmpiW(szBuf, TESTFILE L".exe"), "File with known extension %#x\n", SHGDN_INFOLDER | SHGDN_FORPARSING);
    ResetPath(szPath);

    RegSetDWORD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"HideFileExt", FALSE);
    SHGetSetSettings(NULL, 0, TRUE); // Invalidate SHELLSTATE cache

    szPath[cch] = UNICODE_NULL;
    ResetPath(szPath, L".exe");
    CloseHandle(CreateFileW(szPath, 0, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
    hr = GetDisplayNameOf(szPath, SHGDN_INFOLDER, szBuf, _countof(szBuf));
    ok(hr == S_OK && !lstrcmpiW(szBuf, TESTFILE L".exe"), "File with known extension\n");
    ResetPath(szPath);

    szPath[cch] = UNICODE_NULL;
    ResetPath(szPath, L".lnk");
    CloseHandle(CreateFileW(szPath, 0, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
    hr = GetDisplayNameOf(szPath, SHGDN_INFOLDER, szBuf, _countof(szBuf));
    ok(hr == S_OK && !lstrcmpiW(szBuf, TESTFILE), "File with NeverShowExt extension\n");
    ResetPath(szPath);

    RegSetDWORD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"HideFileExt", OrgHideExt); // Reset
    RegSetDWORD(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"ShowSuperHidden", OrgSuperHidden); // Reset
    SHGetSetSettings(NULL, 0, TRUE); // Invalidate SHELLSTATE cache
}

START_TEST(CFSFolder)
{
    CCoInit ComStaInit;

    TestUninitialized();
    TestInitialize();
    TestGetUIObjectOf();
    TestGetDisplayNameOf();
}
