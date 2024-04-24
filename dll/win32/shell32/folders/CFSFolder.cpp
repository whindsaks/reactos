/*
 * PROJECT:     shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     file system folder
 * COPYRIGHT:   Copyright 1997 Marcus Meissner
 *              Copyright 1998, 1999, 2002 Juergen Schmied
 *              Copyright 2019-2024 Katayama Hirofumi MZ
 *              Copyright 2020 Mark Jansen (mark.jansen@reactos.org)
 */

#include <precomp.h>

WINE_DEFAULT_DEBUG_CHANNEL (shell);

static HRESULT SHELL32_GetCLSIDForDirectory(LPCWSTR pwszDir, LPCWSTR KeyName, CLSID* pclsidFolder);

const UINT64 ITEMMAGIC_WINXP = 0xbeef000400030000ull;
const UINT64 ITEMMAGIC_LAST  = 0xbeef0004ffffffffull;
struct ITEMEXTRA_WINXP
{
    UINT64 Magic;
    WORD CreationDate, CreationTime;
    WORD LastAccessDate, LastAccessTime;
    WORD Offset, Unknown;
};
struct ITEMEXTRA_WINNT6
{
    UINT64 Magic;
    WORD CreationDate, CreationTime;
    WORD LastAccessDate, LastAccessTime;
    WORD Offset, Unknown;
    UINT Unknown2[2];
    UINT SizeHi;
};

static inline CFSFolder::PITEM IsValidItem(PCUITEMID_CHILD pidl)
{
    return _ILGetBaseType(pidl) == PT_FS &&
           pidl->mkid.cb > FIELD_OFFSET(FileStruct, szNames) ? (CFSFolder::PITEM)pidl : NULL;
}

static inline DWORD GetItemFSAttributes16(CFSFolder::PITEM pidl)
{
    return ((FileStruct*)&pidl->mkid.abID[1])->uFileAttribs;
}

static inline bool IsFolder(CFSFolder::PITEM pidl)
{
    return (pidl->mkid.abID[0] & PT_FS_DIR) == PT_FS_DIR ||
           (GetItemFSAttributes16(pidl) & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_NORMAL)) == FILE_ATTRIBUTE_DIRECTORY;
}

static inline DWORD GetItemFSAttributes(CFSFolder::PITEM pidl)
{
    return GetItemFSAttributes16(pidl);
}

/*EXTERN_C LPITEMIDLIST CFSFolder_CreateItem(LPCWSTR Name, const WIN32_FIND_DATAW *pfd, BOOL Directory)
{
    // This works like _ILCreateFromFindDataW in Wine except it creates Unicode items when required (like Windows).
    UINT cchw, cch, cb, cbname, cbalt;
    BOOL notascii = FALSE;
    for (cchw = 0; Name[cchw]; ++cchw)
    {
        notascii |= (Name[cchw] >= 127);
    }
    char ansi[MAX_PATH], *name, altname[8 + 1 + 3 + 1];
    if (notascii)
    {
        cch = cchw;
        cbname = cch * sizeof(WCHAR);
        name = (char*)Name;
    }
    else
    {
tryacp:
        cch = WideCharToMultiByte(CP_ACP, 0, Name, -1, ansi, MAX_PATH, NULL, NULL);
        cbname = cch * sizeof(CHAR);
        name = ansi;
    }
    cbalt = WideCharToMultiByte(CP_ACP, 0, pfd->cAlternateFileName, -1, altname, sizeof(altname), NULL, NULL);
    UINT cbnames = cbname + cbalt;

    BYTE type = (Directory ? PT_FS_DIR : PT_FS_FILE) | (notascii ? PT_FS_UNICODE : 0);
    cb = sizeof(WORD) + sizeof(type) + FIELD_OFFSET(FileStruct, szNames) + cbnames + (cbnames & 1) +
         FIELD_OFFSET(FileStructW, wszName) + (cchw * sizeof(WCHAR)) + sizeof(WORD);
    if (cb > 0xffff)
    {
        if (notascii)
        {
            notascii = FALSE;
            goto tryacp; // It might fit
        }
        return NULL;
    }
    if (cch > MAX_PATH) // We don't have a problem with this but other code might, disallow for now
        return NULL;
    LPITEMIDLIST pidl = (LPITEMIDLIST)SHAlloc(cb + sizeof(WORD));
    if (pidl)
    {
        pidl->mkid.cb = cb;
        *(WORD*)pidl->mkid.abID = type;
        FileStruct *pfs = (FileStruct*)(&pidl->mkid.abID[1]);
        pfs->dwFileSize = pfd->nFileSizeHigh ? 0xffffffff : pfd->nFileSizeLow;
        pfs->uFileDate = pfs->uFileTime = 0;
        if (!FileTimeToDosDateTime(&pfd->ftLastWriteTime, &pfs->uFileDate, &pfs->uFileTime))
        pfs->uFileAttribs = LOWORD(pfd->dwFileAttributes);
        memcpy(pfs->szNames, name, cbname);
        memcpy(pfs->szNames + cbname, altname, cbalt);
        pfs->szNames[cbnames] = 0; // Don't leave junk in alignment byte (if any)

        FileStructW *pfsw = (FileStructW*)(pfs->szNames + cbnames + (cbnames & 0x1));
        *(UINT64*)&pfsw = ITEMMAGIC_WINXP; // Note: Must mask off cbLen if you want to compare against this later
        pfsw->cbLen = FIELD_OFFSET(FileStructW, wszName) + (cchw * sizeof(WCHAR)) + sizeof(WORD);
        pfsw->uCreationDate = pfsw->uCreationTime = 0;
        FileTimeToDosDateTime(&pfd->ftCreationTime, &pfsw->uCreationDate, &pfsw->uCreationTime);
        pfsw->uLastAccessDate = pfsw->uLastAccessTime = 0;
        FileTimeToDosDateTime(&pfd->ftLastAccessTime, &pfsw->uLastAccessDate, &pfsw->uLastAccessTime);
        memcpy(pfsw->wszName, Name, cchw * sizeof(WCHAR));
        *(DWORD*)&pfsw->dummy2 = (DWORD)((SIZE_T)pfsw->wszName - (SIZE_T)pfsw);
        WORD *pOffsetW = (WORD*)((LPBYTE)pidl + pidl->mkid.cb - sizeof(WORD));
        pOffsetW[0] = (LPBYTE)pfsw - (LPBYTE)pidl;
        pOffsetW[1] = 0; // Terminator
    }
    return pidl;
}*/

static UINT GetItemExtraSize(CFSFolder::PITEM pidl)
{
    FileStruct *pfs = (FileStruct*)&pidl->mkid.abID[1];
    BYTE *pend = (BYTE*)pidl + pidl->mkid.cb;
    WORD cbOffset = *(WORD*)(pend - sizeof(WORD));
    FileStructW *pfsw = (FileStructW*)((BYTE*)pidl + cbOffset);
    if ((BYTE*)pfsw > (BYTE*)&pfs->szNames[1 + 1] && (BYTE*)&pfsw->wszName[2] < pend)
    {
        UINT64 magic = *(UINT64*)pfsw;
        if (magic >= ITEMMAGIC_WINXP && magic < ITEMMAGIC_LAST && pfsw->cbLen > FIELD_OFFSET(FileStructW, wszName))
        {
            return ((ITEMEXTRA_WINXP*)pfsw)->Offset;
        }
    }
    // To remain compatible with old Wine/ROS PIDLs that may exist in .lnk files, check the old format
    return _ILGetFileStructW((LPITEMIDLIST)pidl) ? FIELD_OFFSET(FileStructW, wszName) : 0;
}

static inline ITEMEXTRA_WINNT6* GetItemExtra(CFSFolder::PITEM pidl)
{
    return (ITEMEXTRA_WINNT6*)((BYTE*)pidl + *(WORD*)((BYTE*)pidl + pidl->mkid.cb - sizeof(WORD)));
}

static LPCWSTR GetItemFSName(CFSFolder::PITEM pidl, LPWSTR Buf, UINT MaxBuf)
{
    UINT cbx = GetItemExtraSize(pidl);
    if (cbx >= FIELD_OFFSET(FileStructW, wszName))
        return (LPCWSTR)((BYTE*)GetItemExtra(pidl) + cbx);
    FileStruct *pfs = (FileStruct*)&pidl->mkid.abID[1];
    if (_ILFastGetType(pidl) & PT_FS_UNICODE)
        return (LPCWSTR)pfs->szNames;
    SHAnsiToUnicode(pfs->szNames, Buf, MaxBuf);
    return Buf;
}

static void GetItemFSNameToBuffer(CFSFolder::PITEM pidl, LPWSTR Buf, UINT MaxBuf)
{
    LPCWSTR name = GetItemFSName(pidl, Buf, MaxBuf);
    if (name != Buf)
        lstrcpynW(Buf, name, MaxBuf);
}

static HRESULT GetItemSizeHelper(CFSFolder::PITEM pidl, UINT64 &size)
{
    ULARGE_INTEGER li;
    li.LowPart = ((FileStruct*)&pidl->mkid.abID[1])->dwFileSize;
    UINT cbx = GetItemExtraSize(pidl);
    if (cbx >= FIELD_OFFSET(ITEMEXTRA_WINNT6, SizeHi) + sizeof(UINT))
    {
        li.HighPart = GetItemExtra(pidl)->SizeHi;
        size = li.QuadPart;
        return S_OK;
    }
    if (li.LowPart != 0xffffffff)
    {
        size = li.LowPart;
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA); // Call GetFileSize
}

static LPCWSTR GetItemExtension(CFSFolder::PITEM pidl, LPWSTR Buf, UINT MaxBuf)
{
    if (!IsFolder(pidl))
    {
        LPCWSTR ext = PathFindExtensionW(GetItemFSName(pidl, Buf, MaxBuf));
        return *ext ? ext : NULL;
    }
    return NULL;
}

// FIXME: It is rarely correct to open .ext\foo before trying progid\foo first!
HKEY OpenKeyFromFileType(LPCWSTR pExtension, LPCWSTR KeyName)
{
    HKEY hkey;
    WCHAR FullName[MAX_PATH * 2];
    DWORD dwSize = sizeof(FullName);
    wsprintf(FullName, L"%s\\%s", pExtension, KeyName);

    LONG res = RegOpenKeyExW(HKEY_CLASSES_ROOT, FullName, 0, KEY_READ, &hkey);
    if (!res)
        return hkey;

    res = RegGetValueW(HKEY_CLASSES_ROOT, pExtension, NULL, RRF_RT_REG_SZ, NULL, FullName, &dwSize);
    if (res)
    {
        WARN("Failed to get progid for extension %S (%x), error %d\n", pExtension, pExtension, res);
        return NULL;
    }

    wcscat(FullName, L"\\");
    wcscat(FullName, KeyName);

    hkey = NULL;
    res = RegOpenKeyExW(HKEY_CLASSES_ROOT, FullName, 0, KEY_READ, &hkey);
    if (res)
        WARN("Could not open key %S for extension %S\n", KeyName, pExtension);

    return hkey;
}

HRESULT GetCLSIDForFileTypeFromExtension(LPCWSTR pExtension, LPCWSTR KeyName, CLSID* pclsid)
{
    HKEY hkeyProgId = OpenKeyFromFileType(pExtension, KeyName);
    if (!hkeyProgId)
    {
        WARN("OpenKeyFromFileType failed for key %S\n", KeyName);
        return S_FALSE;
    }

    WCHAR wszCLSIDValue[CHARS_IN_GUID];
    DWORD dwSize = sizeof(wszCLSIDValue);
    LONG res = RegGetValueW(hkeyProgId, NULL, NULL, RRF_RT_REG_SZ, NULL, wszCLSIDValue, &dwSize);
    RegCloseKey(hkeyProgId);
    if (res)
    {
        ERR("OpenKeyFromFileType succeeded but RegGetValueW failed\n");
        return S_FALSE;
    }

#if 0
    {
        res = RegGetValueW(HKEY_LOCAL_MACHINE,
                           L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
                           wszCLSIDValue,
                           RRF_RT_REG_SZ,
                           NULL,
                           NULL,
                           NULL);
        if (res != ERROR_SUCCESS)
        {
            ERR("DropHandler extension %S not approved\n", wszName); // FIXME: DropHandler?
            return E_ACCESSDENIED;
        }
    }
#endif

    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked",
                     wszCLSIDValue,
                     RRF_RT_REG_SZ,
                     NULL,
                     NULL,
                     NULL) == ERROR_SUCCESS)
    {
        ERR("Extension %S  not approved\n", wszCLSIDValue);
        return E_ACCESSDENIED;
    }

    HRESULT hres = CLSIDFromString (wszCLSIDValue, pclsid);
    if (FAILED_UNEXPECTEDLY(hres))
        return hres;

    return S_OK;
}

// FIXME: Why does this take a PCUIDLIST_RELATIVE but treats it as a PCUITEMID_CHILD?
HRESULT GetCLSIDForFileType(PCUIDLIST_RELATIVE pidl, LPCWSTR KeyName, CLSID* pclsid)
{
    CFSFolder::PITEM item = IsValidItem(pidl);
    if (!item)
        return E_INVALIDARG;
    WCHAR extbuf[MAX_PATH];
    LPCWSTR pExtension = GetItemExtension(item, extbuf, _countof(extbuf));
    if (!pExtension)
        return S_FALSE;
    return GetCLSIDForFileTypeFromExtension(pExtension, KeyName, pclsid);
}

static HRESULT GetItemCLSID(CFSFolder::PITEM pidl, CLSID &clsid)
{
    return E_NOTIMPL; // FIXME
}

static HRESULT
getDefaultIconLocation(LPWSTR szIconFile, UINT cchMax, int *piIndex, UINT uFlags)
{
    if (!HLM_GetIconW(IDI_SHELL_FOLDER - 1, szIconFile, cchMax, piIndex))
    {
        if (!HCR_GetIconW(L"Folder", szIconFile, NULL, cchMax, piIndex))
        {
            StringCchCopyW(szIconFile, cchMax, swShell32Name);
            *piIndex = -IDI_SHELL_FOLDER;
        }
    }

    if (uFlags & GIL_OPENICON)
    {
        // next icon
        if (*piIndex < 0)
            (*piIndex)--;
        else
            (*piIndex)++;
    }

    return S_OK;
}

static BOOL
getShellClassInfo(LPCWSTR Entry, LPWSTR pszValue, DWORD cchValueLen, LPCWSTR IniFile)
{
    return GetPrivateProfileStringW(L".ShellClassInfo", Entry, NULL, pszValue, cchValueLen, IniFile);
}

static HRESULT
getIconLocationForFolder(IShellFolder * psf, PCITEMID_CHILD pidl, UINT uFlags,
                         LPWSTR szIconFile, UINT cchMax, int *piIndex, UINT *pwFlags)
{
    DWORD dwFileAttrs, shdid;
    WCHAR wszPath[MAX_PATH];
    WCHAR wszIniFullPath[MAX_PATH + 1 + sizeof("desktop.ini") - 1];

    if (uFlags & GIL_DEFAULTICON)
        goto Quit;

    pidl = (PCITEMID_CHILD)ILFindLastID(pidl); // FIXME: Why is this calling ILFindLastID when the parameter is a PCITEMID_CHILD?
    shdid = _ILItemGetFSItemType(pidl);
    if (!shdid || shdid == SHDID_FS_FILE)
        goto Quit;

    // get path
    if (!ILGetDisplayNameExW(psf, pidl, wszPath, 0))
        goto Quit;
    if (!PathIsDirectoryW(wszPath))
        goto Quit;

    // read-only or system folder?
    dwFileAttrs = GetItemFSAttributes((CFSFolder::PITEM)pidl);
    if ((dwFileAttrs & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY)) == 0)
        goto Quit;

    // build the full path of ini file
    StringCchCopyW(wszIniFullPath, _countof(wszIniFullPath), wszPath);
    PathAppendW(wszIniFullPath, L"desktop.ini");

    WCHAR wszValue[MAX_PATH], wszTemp[MAX_PATH];
    if (getShellClassInfo(L"IconFile", wszValue, _countof(wszValue), wszIniFullPath))
    {
        // wszValue --> wszTemp
        ExpandEnvironmentStringsW(wszValue, wszTemp, _countof(wszTemp));

        // wszPath + wszTemp --> wszPath
        if (PathIsRelativeW(wszTemp))
            PathAppendW(wszPath, wszTemp);
        else
            StringCchCopyW(wszPath, _countof(wszPath), wszTemp);

        // wszPath --> szIconFile
        GetFullPathNameW(wszPath, cchMax, szIconFile, NULL);

        *piIndex = GetPrivateProfileIntW(L".ShellClassInfo", L"IconIndex", 0, wszIniFullPath);
        return S_OK;
    }
    else if (getShellClassInfo(L"CLSID", wszValue, _countof(wszValue), wszIniFullPath) &&
             HCR_GetIconW(wszValue, szIconFile, NULL, cchMax, piIndex))
    {
        return S_OK;
    }
    else if (getShellClassInfo(L"CLSID2", wszValue, _countof(wszValue), wszIniFullPath) &&
             HCR_GetIconW(wszValue, szIconFile, NULL, cchMax, piIndex))
    {
        return S_OK;
    }
    else if (getShellClassInfo(L"IconResource", wszValue, _countof(wszValue), wszIniFullPath))
    {
        // wszValue --> wszTemp
        ExpandEnvironmentStringsW(wszValue, wszTemp, _countof(wszTemp));

        // parse the icon location
        *piIndex = PathParseIconLocationW(wszTemp);

        // wszPath + wszTemp --> wszPath
        if (PathIsRelativeW(wszTemp))
            PathAppendW(wszPath, wszTemp);
        else
            StringCchCopyW(wszPath, _countof(wszPath), wszTemp);

        // wszPath --> szIconFile
        GetFullPathNameW(wszPath, cchMax, szIconFile, NULL);
        return S_OK;
    }

Quit:
    return getDefaultIconLocation(szIconFile, cchMax, piIndex, uFlags);
}

HRESULT CFSExtractIcon_CreateInstance(IShellFolder * psf, LPCITEMIDLIST pidl, REFIID iid, LPVOID * ppvOut)
{
    CComPtr<IDefaultExtractIconInit> initIcon;
    HRESULT hr;
    int icon_idx = 0;
    UINT flags = 0; // FIXME: Use it!
    WCHAR wTemp[MAX_PATH] = L"";
    DWORD shdid = _ILItemGetFSItemType(pidl);
    if (!shdid)
        return E_INVALIDARG;

    hr = SHCreateDefaultExtractIcon(IID_PPV_ARG(IDefaultExtractIconInit,&initIcon));
    if (FAILED(hr))
        return hr;

    if (shdid == SHDID_FS_DIRECTORY)
    {
        if (SUCCEEDED(getIconLocationForFolder(psf,
                          pidl, 0, wTemp, _countof(wTemp),
                          &icon_idx,
                          &flags)))
        {
            initIcon->SetNormalIcon(wTemp, icon_idx);
            // FIXME: if/when getIconLocationForFolder does something for
            //        GIL_FORSHORTCUT, code below should be uncommented. and
            //        the following line removed.
            initIcon->SetShortcutIcon(wTemp, icon_idx);
        }
        if (SUCCEEDED(getIconLocationForFolder(psf,
                          pidl, GIL_DEFAULTICON, wTemp, _countof(wTemp),
                          &icon_idx,
                          &flags)))
        {
            initIcon->SetDefaultIcon(wTemp, icon_idx);
        }
        // if (SUCCEEDED(getIconLocationForFolder(psf,
        //                   pidl, GIL_FORSHORTCUT, wTemp, _countof(wTemp),
        //                   &icon_idx,
        //                   &flags)))
        // {
        //     initIcon->SetShortcutIcon(wTemp, icon_idx);
        // }
        if (SUCCEEDED(getIconLocationForFolder(psf,
                          pidl, GIL_OPENICON, wTemp, _countof(wTemp),
                          &icon_idx,
                          &flags)))
        {
            initIcon->SetOpenIcon(wTemp, icon_idx);
        }
    }
    else
    {
        WCHAR extbuf[MAX_PATH];
        LPCWSTR pExtension = GetItemExtension((CFSFolder::PITEM)pidl, extbuf, _countof(extbuf));
        HKEY hkey = pExtension ? OpenKeyFromFileType(pExtension, L"DefaultIcon") : NULL;
        if (!hkey)
            WARN("Could not open DefaultIcon key!\n");

        DWORD dwSize = sizeof(wTemp);
        if (hkey && !SHQueryValueExW(hkey, NULL, NULL, NULL, wTemp, &dwSize))
        {
            WCHAR sNum[5];
            if (ParseFieldW (wTemp, 2, sNum, 5))
                icon_idx = _wtoi(sNum);
            else
                icon_idx = 0; /* sometimes the icon number is missing */
            ParseFieldW (wTemp, 1, wTemp, MAX_PATH);
            PathUnquoteSpacesW(wTemp);

            if (!wcscmp(L"%1", wTemp)) /* icon is in the file */
            {
                ILGetDisplayNameExW(psf, pidl, wTemp, ILGDN_FORPARSING);
                icon_idx = 0;

                INT ret = PrivateExtractIconsW(wTemp, 0, 0, 0, NULL, NULL, 0, 0);
                if (ret <= 0)
                {
                    StringCbCopyW(wTemp, sizeof(wTemp), swShell32Name);
                    if (lstrcmpiW(pExtension, L".exe") == 0 || lstrcmpiW(pExtension, L".scr") == 0)
                        icon_idx = -IDI_SHELL_EXE;
                    else
                        icon_idx = -IDI_SHELL_DOCUMENT;
                }
            }

            initIcon->SetNormalIcon(wTemp, icon_idx);
        }
        else
        {
            initIcon->SetNormalIcon(swShell32Name, 0);
        }

        if (hkey)
            RegCloseKey(hkey);
    }

    return initIcon->QueryInterface(iid, ppvOut);
}

/*
CFileSysEnum should do an initial FindFirstFile and do a FindNextFile as each file is
returned by Next. When the enumerator is created, it can do numerous additional operations
including formatting a drive, reconnecting a network share drive, and requesting a disk
be inserted in a removable drive.
*/


class CFileSysEnum :
    public CEnumIDListBase
{
private:
    HRESULT _AddFindResult(LPWSTR sParentDir, const WIN32_FIND_DATAW& FindData, DWORD dwFlags)
    {
#define SUPER_HIDDEN (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)

        // Does it need special handling because it is hidden?
        if (FindData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
        {
            DWORD dwHidden = FindData.dwFileAttributes & SUPER_HIDDEN;

            // Is it hidden, but are we not asked to include hidden?
            if (dwHidden == FILE_ATTRIBUTE_HIDDEN && !(dwFlags & SHCONTF_INCLUDEHIDDEN))
                return S_OK;

            // Is it a system file, but are we not asked to include those?
            if (dwHidden == SUPER_HIDDEN && !(dwFlags & SHCONTF_INCLUDESUPERHIDDEN))
                return S_OK;
        }

        BOOL bDirectory = (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        HRESULT hr;
        if (bDirectory)
        {
            // Skip the current and parent directory nodes
            if (!strcmpW(FindData.cFileName, L".") || !strcmpW(FindData.cFileName, L".."))
                return S_OK;

            // Does this directory need special handling?
            if ((FindData.dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY)) != 0)
            {
                WCHAR Tmp[MAX_PATH];
                CLSID clsidFolder;

                PathCombineW(Tmp, sParentDir, FindData.cFileName);

                hr = SHELL32_GetCLSIDForDirectory(Tmp, L"CLSID", &clsidFolder);
                if (SUCCEEDED(hr))
                {
                    ERR("Got CLSID override for '%S'\n", Tmp);
                }
            }
        }
        else
        {
            CLSID clsidFile;
            LPWSTR pExtension = PathFindExtensionW(FindData.cFileName);
            if (pExtension)
            {
                // FIXME: Cache this?
                hr = GetCLSIDForFileTypeFromExtension(pExtension, L"CLSID", &clsidFile);
                if (hr == S_OK)
                {
                    HKEY hkey;
                    hr = SHRegGetCLSIDKeyW(clsidFile, L"ShellFolder", FALSE, FALSE, &hkey);
                    if (SUCCEEDED(hr))
                    {
                        ::RegCloseKey(hkey);

                        // This should be presented as directory!
                        bDirectory = TRUE;
                        TRACE("Treating '%S' as directory!\n", FindData.cFileName);
                    }
                }
            }
        }

        LPITEMIDLIST pidl = NULL;
        if (bDirectory)
        {
            if (dwFlags & SHCONTF_FOLDERS)
            {
                TRACE("(%p)->  (folder=%s)\n", this, debugstr_w(FindData.cFileName));
                pidl = _ILCreateFromFindDataW(&FindData);
            }
        }
        else
        {
            if (dwFlags & SHCONTF_NONFOLDERS)
            {
                TRACE("(%p)->  (file  =%s)\n", this, debugstr_w(FindData.cFileName));
                pidl = _ILCreateFromFindDataW(&FindData);
            }
        }

        if (pidl && !AddToEnumList(pidl))
        {
            FAILED_UNEXPECTEDLY(E_FAIL);
            return E_FAIL;
        }

        return S_OK;
    }

public:
    CFileSysEnum()
    {

    }

    ~CFileSysEnum()
    {
    }

    HRESULT WINAPI Initialize(LPWSTR sPathTarget, DWORD dwFlags)
    {
        TRACE("(%p)->(path=%s flags=0x%08x)\n", this, debugstr_w(sPathTarget), dwFlags);

        if (!sPathTarget || !sPathTarget[0])
        {
            WARN("No path for CFileSysEnum, empty result!\n");
            return S_FALSE;
        }

        WCHAR szFindPattern[MAX_PATH];
        HRESULT hr = StringCchCopyW(szFindPattern, _countof(szFindPattern), sPathTarget);
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        /* FIXME: UNSAFE CRAP */
        PathAddBackslashW(szFindPattern);

        hr = StringCchCatW(szFindPattern, _countof(szFindPattern), L"*.*");
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;


        WIN32_FIND_DATAW FindData;
        HANDLE hFind = FindFirstFileW(szFindPattern, &FindData);
        if (hFind == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(GetLastError());

        do
        {
            hr = _AddFindResult(sPathTarget, FindData, dwFlags);

            if (FAILED_UNEXPECTEDLY(hr))
                break;

        } while(FindNextFileW(hFind, &FindData));

        if (SUCCEEDED(hr))
        {
            DWORD dwError = GetLastError();
            if (dwError != ERROR_NO_MORE_FILES)
            {
                hr = HRESULT_FROM_WIN32(dwError);
                FAILED_UNEXPECTEDLY(hr);
            }
        }
        TRACE("(%p)->(hr=0x%08x)\n", this, hr);
        FindClose(hFind);
        return hr;
    }

    BEGIN_COM_MAP(CFileSysEnum)
        COM_INTERFACE_ENTRY_IID(IID_IEnumIDList, IEnumIDList)
    END_COM_MAP()
};


/***********************************************************************
 *   IShellFolder implementation
 */

CFSFolder::CFSFolder()
{
    m_pclsid = &CLSID_ShellFSFolder;
    m_sPathTarget = NULL;
    m_pidlRoot = NULL;
    m_bGroupPolicyActive = 0;
}

CFSFolder::~CFSFolder()
{
    TRACE("-- destroying IShellFolder(%p)\n", this);

    SHFree(m_pidlRoot);
    SHFree(m_sPathTarget);
}


static const shvheader GenericSFHeader[] = {
    {IDS_SHV_COLUMN_NAME, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 15},
    {IDS_SHV_COLUMN_TYPE, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 10},
    {IDS_SHV_COLUMN_SIZE, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_RIGHT, 10},
    {IDS_SHV_COLUMN_MODIFIED, SHCOLSTATE_TYPE_DATE | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 12},
    {IDS_SHV_COLUMN_ATTRIBUTES, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 10},
    {IDS_SHV_COLUMN_COMMENTS, SHCOLSTATE_TYPE_STR, LVCFMT_LEFT, 10}
};

#define GENERICSHELLVIEWCOLUMNS 6

static HRESULT SHELL32_GetCLSIDForDirectory(LPCWSTR pwszDir, LPCWSTR KeyName, CLSID* pclsidFolder)
{
    WCHAR wszCLSIDValue[CHARS_IN_GUID];
    WCHAR wszDesktopIni[MAX_PATH];

    StringCchCopyW(wszDesktopIni, MAX_PATH, pwszDir);
    StringCchCatW(wszDesktopIni, MAX_PATH, L"\\desktop.ini");

    if (GetPrivateProfileStringW(L".ShellClassInfo",
                                 KeyName,
                                 L"",
                                 wszCLSIDValue,
                                 CHARS_IN_GUID,
                                 wszDesktopIni))
    {
        return CLSIDFromString(wszCLSIDValue, pclsidFolder);
    }
    return E_FAIL;
}

// FIXME: This should be a method in CFSFolder
static HRESULT SHELL32_GetFSItemAttributes(IShellFolder * psf, LPCITEMIDLIST pidl, LPDWORD pdwAttributes)
{
    DWORD dwFileAttributes, dwShellAttributes;
    DWORD shdid = _ILItemGetFSItemType(pidl);
    BOOL bDirectory = shdid == SHDID_FS_DIRECTORY;

    if (!shdid)
    {
        ERR("Got wrong type of pidl!\n");
        *pdwAttributes &= SFGAO_CANLINK;
        return S_OK; // FIXME: Why is this a success?
    }

    dwFileAttributes = GetItemFSAttributes((CFSFolder::PITEM)pidl);

    /* Set common attributes */
    dwShellAttributes = SFGAO_CANCOPY | SFGAO_CANMOVE | SFGAO_CANLINK | SFGAO_CANRENAME | SFGAO_CANDELETE |
                        SFGAO_HASPROPSHEET | SFGAO_DROPTARGET | SFGAO_FILESYSTEM;

    WCHAR extbuf[MAX_PATH];
    LPCWSTR pExtension = NULL;
    if (!bDirectory || (*pdwAttributes && SFGAO_LINK))
    {
        pExtension = GetItemExtension((CFSFolder::PITEM)pidl, extbuf, _countof(extbuf));
    }

    if (!bDirectory)
    {
        // https://git.reactos.org/?p=reactos.git;a=blob;f=dll/shellext/zipfldr/res/zipfldr.rgs;hb=032b5aacd233cd7b83ab6282aad638c161fdc400#l9
        if (pExtension)
        {
            CLSID clsidFile;
            // FIXME: Cache this?
            HRESULT hr = GetCLSIDForFileTypeFromExtension(pExtension, L"CLSID", &clsidFile);
            if (hr == S_OK)
            {
                HKEY hkey;
                hr = SHRegGetCLSIDKeyW(clsidFile, L"ShellFolder", FALSE, FALSE, &hkey);
                if (SUCCEEDED(hr))
                {
                    DWORD dwAttributes = 0;
                    DWORD dwSize = sizeof(dwAttributes);
                    LSTATUS Status;

                    Status = SHRegGetValueW(hkey, NULL, L"Attributes", RRF_RT_REG_DWORD, NULL, &dwAttributes, &dwSize);
                    if (Status == STATUS_SUCCESS)
                    {
                        TRACE("Augmenting '%S' with dwAttributes=0x%x\n", pExtension, dwAttributes);
                        dwShellAttributes |= dwAttributes; // FIXME: Should it really |= or just =?
                    }
                    ::RegCloseKey(hkey);

                    // This should be presented as directory!
                    bDirectory = TRUE;
                    TRACE("Treating '%S' as directory!\n", pExtension);
                }
            }
        }
    }

    // This is a directory
    if (bDirectory)
    {
        dwShellAttributes |= (SFGAO_FOLDER | /*SFGAO_HASSUBFOLDER |*/ SFGAO_STORAGE);

        // Is this a real directory?
        if (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            dwShellAttributes |= (SFGAO_FILESYSANCESTOR | SFGAO_STORAGEANCESTOR);
        }
    }
    else
    {
        dwShellAttributes |= SFGAO_STREAM;
    }

    if (dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
        dwShellAttributes |=  SFGAO_HIDDEN;

    if (dwFileAttributes & FILE_ATTRIBUTE_READONLY)
        dwShellAttributes |=  SFGAO_READONLY;

    if (SFGAO_LINK & *pdwAttributes)
    {
        if (pExtension && !lstrcmpiW(pExtension, L".lnk"))
            dwShellAttributes |= SFGAO_LINK;
    }

    if (SFGAO_HASSUBFOLDER & *pdwAttributes)
    {
        CComPtr<IShellFolder> psf2;
        if (SUCCEEDED(psf->BindToObject(pidl, 0, IID_PPV_ARG(IShellFolder, &psf2))))
        {
            CComPtr<IEnumIDList> pEnumIL;
            if (SUCCEEDED(psf2->EnumObjects(0, SHCONTF_FOLDERS, &pEnumIL)))
            {
                if (pEnumIL->Skip(1) == S_OK)
                    dwShellAttributes |= SFGAO_HASSUBFOLDER;
            }
        }
    }

    *pdwAttributes = dwShellAttributes;

    TRACE ("-- 0x%08x\n", *pdwAttributes);
    return S_OK;
}

HRESULT CFSFolder::_ParseSimple(
    _In_ LPOLESTR lpszDisplayName,
    _Out_ WIN32_FIND_DATAW *pFind,
    _Out_ LPITEMIDLIST *ppidl)
{
    HRESULT hr;
    LPWSTR pchNext = lpszDisplayName;

    *ppidl = NULL;

    const DWORD finalattr = pFind->dwFileAttributes;
    const DWORD finalsizelo = pFind->nFileSizeLow;
    LPITEMIDLIST pidl;
    for (hr = S_OK; SUCCEEDED(hr); hr = SHILAppend(pidl, ppidl))
    {
        hr = Shell_NextElement(&pchNext, pFind->cFileName, _countof(pFind->cFileName), FALSE);
        if (hr != S_OK)
            break;

        pFind->dwFileAttributes = pchNext ? FILE_ATTRIBUTE_DIRECTORY : finalattr;
        pFind->nFileSizeLow = pchNext ? 0 : finalsizelo;
        pidl = _ILCreateFromFindDataW(pFind);
        if (!pidl)
        {
            hr = E_OUTOFMEMORY;
            break;
        }
    }

    if (SUCCEEDED(hr))
        return S_OK;

    if (*ppidl)
    {
        ILFree(*ppidl);
        *ppidl = NULL;
    }

    return hr;
}

BOOL CFSFolder::_GetFindDataFromName(_In_ LPCWSTR pszName, _Out_ WIN32_FIND_DATAW *pFind)
{
    WCHAR szPath[MAX_PATH];
    lstrcpynW(szPath, m_sPathTarget, _countof(szPath));
    PathAppendW(szPath, pszName);

    HANDLE hFind = ::FindFirstFileW(szPath, pFind);
    if (hFind == INVALID_HANDLE_VALUE)
        return FALSE;

    ::FindClose(hFind);
    return TRUE;
}

HRESULT CFSFolder::_CreateIDListFromName(LPCWSTR pszName, DWORD attrs, IBindCtx *pbc, LPITEMIDLIST *ppidl)
{
    *ppidl = NULL;

    if (PathIsDosDevice(pszName))
        return HRESULT_FROM_WIN32(ERROR_BAD_DEVICE);

    WIN32_FIND_DATAW FindData = { 0 };

    HRESULT hr = S_OK;
    if (attrs == ULONG_MAX) // Invalid attributes
    {
        if (!_GetFindDataFromName(pszName, &FindData))
            hr = HRESULT_FROM_WIN32(::GetLastError());
    }
    else // Pretend as an item of attrs
    {
        StringCchCopyW(FindData.cFileName, _countof(FindData.cFileName), pszName);
        FindData.dwFileAttributes = attrs;
    }

    if (FAILED(hr))
        return hr;

    *ppidl = _ILCreateFromFindDataW(&FindData);
    if (!*ppidl)
        return E_OUTOFMEMORY;

    return S_OK;
}

UINT64 CFSFolder::_GetSize(_In_ PITEM pidl)
{
    UINT64 size;
    HRESULT hr = GetItemSizeHelper(pidl, size);
    if (SUCCEEDED(hr))
        return size;
    WIN32_FIND_DATAW fd;
    WCHAR buf[MAX_PATH];
    if (_GetFindDataFromName(GetItemFSName(pidl, buf, _countof(buf)), &fd))
    {
        ULARGE_INTEGER li;
        li.LowPart = fd.nFileSizeLow;
        li.HighPart = fd.nFileSizeHigh;
        return li.QuadPart;
    }
    return 0;
}

HRESULT CFSFolder::_GetFindData(_In_ PITEM pidl, WIN32_FIND_DATAW &fd, BOOL CanHitDisk)
{
    LPCWSTR name = GetItemFSName(pidl, fd.cFileName, MAX_PATH);
    if (CanHitDisk && _GetFindDataFromName(name, &fd))
        return S_OK;
    ZeroMemory(&fd, FIELD_OFFSET(WIN32_FIND_DATAW, cFileName));
    FileStruct *pfs = (FileStruct*)(&pidl->mkid.abID[1]);
    UINT64 size;
    GetItemSizeHelper(pidl, size);
    ULARGE_INTEGER li;
    li.QuadPart = size;
    fd.nFileSizeLow = li.LowPart;
    fd.nFileSizeHigh = li.HighPart;
    fd.dwFileAttributes = GetItemFSAttributes(pidl);
    DosDateTimeToFileTime(pfs->uFileDate, pfs->uFileTime, &fd.ftLastWriteTime);
    if (name != fd.cFileName)
        lstrcpynW(fd.cFileName, name, MAX_PATH);
    fd.cAlternateFileName[0] = '\0'; // TODO: MultiByteToWideChar if the altname is there
    if (GetItemExtraSize(pidl))
    {
        ITEMEXTRA_WINXP &x = *(ITEMEXTRA_WINXP*)GetItemExtra(pidl);
        DosDateTimeToFileTime(x.CreationDate, x.CreationTime, &fd.ftCreationTime);
        DosDateTimeToFileTime(x.LastAccessDate, x.LastAccessTime, &fd.ftLastAccessTime);
        return S_OK;
    }
    return S_FALSE;
}

/**************************************************************************
* CFSFolder::ParseDisplayName {SHELL32}
*
* Parse a display name.
*
* PARAMS
*  hwndOwner       [in]  Parent window for any message's
*  pbc             [in]  optional FileSystemBindData context
*  lpszDisplayName [in]  Unicode displayname.
*  pchEaten        [out] (unicode) characters processed
*  ppidl           [out] complex pidl to item
*  pdwAttributes   [out] items attributes
*
* NOTES
*  Every folder tries to parse only its own (the leftmost) pidl and creates a
*  subfolder to evaluate the remaining parts.
*  Now we can parse into namespaces implemented by shell extensions
*
*  Behaviour on win98: lpszDisplayName=NULL -> crash
*                      lpszDisplayName="" -> returns mycoputer-pidl
*
* FIXME
*    pdwAttributes is not set
*    pchEaten is not set like in windows
*/
HRESULT WINAPI CFSFolder::ParseDisplayName(HWND hwndOwner,
        LPBC pbc,
        LPOLESTR lpszDisplayName,
        DWORD *pchEaten, PIDLIST_RELATIVE *ppidl,
        DWORD *pdwAttributes)
{
    TRACE ("(%p)->(HWND=%p,%p,%p=%s,%p,pidl=%p,%p)\n",
           this, hwndOwner, pbc, lpszDisplayName, debugstr_w (lpszDisplayName),
           pchEaten, ppidl, pdwAttributes);

    if (!ppidl)
        return E_INVALIDARG;

    *ppidl = NULL;

    if (!lpszDisplayName)
        return E_INVALIDARG;

    HRESULT hr;
    WIN32_FIND_DATAW FindData;
    if (SHIsFileSysBindCtx(pbc, &FindData) == S_OK)
    {
        CComHeapPtr<ITEMIDLIST> pidlTemp;
        hr = _ParseSimple(lpszDisplayName, &FindData, &pidlTemp);
        if (SUCCEEDED(hr) && pdwAttributes && *pdwAttributes)
        {
            LPCITEMIDLIST pidlLast = ILFindLastID(pidlTemp);
            GetAttributesOf(1, &pidlLast, pdwAttributes);
        }

        if (SUCCEEDED(hr))
            *ppidl = pidlTemp.Detach();
    }
    else
    {
        INT cchElement = lstrlenW(lpszDisplayName) + 1;
        LPWSTR pszElement = (LPWSTR)alloca(cchElement * sizeof(WCHAR));
        LPWSTR pchNext = lpszDisplayName;
        hr = Shell_NextElement(&pchNext, pszElement, cchElement, TRUE);
        if (FAILED(hr))
            return hr;

        hr = _CreateIDListFromName(pszElement, ULONG_MAX, pbc, ppidl);
        if (FAILED(hr))
        {
            if (pchNext) // Is there the next element?
            {
                // pszElement seems like a directory
                if (_GetFindDataFromName(pszElement, &FindData) &&
                    (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    hr = _CreateIDListFromName(pszElement, FILE_ATTRIBUTE_DIRECTORY, pbc, ppidl);
                }
            }
            else
            {
                // pszElement seems like a non-directory
                if ((hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
                     hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) &&
                    (BindCtx_GetMode(pbc, 0) & STGM_CREATE))
                {
                    // Pretend like a normal file
                    hr = _CreateIDListFromName(pszElement, FILE_ATTRIBUTE_NORMAL, pbc, ppidl);
                }
            }
        }

        if (SUCCEEDED(hr))
        {
            if (pchNext) // Is there next?
            {
                CComPtr<IShellFolder> psfChild;
                hr = BindToObject(*ppidl, pbc, IID_PPV_ARG(IShellFolder, &psfChild));
                if (FAILED(hr))
                    return hr;

                DWORD chEaten;
                CComHeapPtr<ITEMIDLIST> pidlChild;
                hr = psfChild->ParseDisplayName(hwndOwner, pbc, pchNext, &chEaten, &pidlChild,
                                                pdwAttributes);

                // Append pidlChild to ppidl
                if (SUCCEEDED(hr))
                    hr = SHILAppend(pidlChild.Detach(), ppidl);
            }
            else if (pdwAttributes && *pdwAttributes)
            {
                GetAttributesOf(1, (LPCITEMIDLIST*)ppidl, pdwAttributes);
            }
        }
    }

    TRACE("(%p)->(-- pidl=%p ret=0x%08x)\n", this, ppidl ? *ppidl : 0, hr);

    return hr;
}

/**************************************************************************
* CFSFolder::EnumObjects
* PARAMETERS
*  HWND          hwndOwner,    //[in ] Parent Window
*  DWORD         grfFlags,     //[in ] SHCONTF enumeration mask
*  LPENUMIDLIST* ppenumIDList  //[out] IEnumIDList interface
*/
HRESULT WINAPI CFSFolder::EnumObjects(
    HWND hwndOwner,
    DWORD dwFlags,
    LPENUMIDLIST *ppEnumIDList)
{
    return ShellObjectCreatorInit<CFileSysEnum>(m_sPathTarget, dwFlags, IID_PPV_ARG(IEnumIDList, ppEnumIDList));
}

/**************************************************************************
* CFSFolder::BindToObject
* PARAMETERS
*  LPCITEMIDLIST pidl,       //[in ] relative pidl to open
*  LPBC          pbc,        //[in ] optional FileSystemBindData context
*  REFIID        riid,       //[in ] Initial Interface
*  LPVOID*       ppvObject   //[out] Interface*
*/
HRESULT WINAPI CFSFolder::BindToObject(
    PCUIDLIST_RELATIVE pidl,
    LPBC pbc,
    REFIID riid,
    LPVOID * ppvOut)
{
    TRACE("(%p)->(pidl=%p,%p,%s,%p)\n", this, pidl, pbc,
          shdebugstr_guid(&riid), ppvOut);

    CComPtr<IShellFolder> pSF;
    HRESULT hr;

    if (!m_pidlRoot || !ppvOut || !pidl || !pidl->mkid.cb)
    {
        ERR("CFSFolder::BindToObject: Invalid parameters\n");
        return E_INVALIDARG;
    }
    *ppvOut = NULL;
    DWORD shdid = _ILItemGetFSItemType(pidl);
    if (!shdid)
    {
        ERR("CFSFolder::BindToObject: Invalid pidl!\n");
        return E_INVALIDARG;
    }
    UINT fsatt = GetItemFSAttributes((PITEM)pidl);
    WCHAR namebuf[MAX_PATH];
    LPCWSTR name = GetItemFSName((PITEM)pidl, namebuf, _countof(namebuf));

    /* Create the target folder info */
    PERSIST_FOLDER_TARGET_INFO pfti = {0};
    pfti.dwAttributes = -1;
    pfti.csidl = -1;
    PathCombineW(pfti.szTargetParsingName, m_sPathTarget, name);

    /* Get the CLSID to bind to */
    CLSID clsidFolder;
    if (shdid == SHDID_FS_DIRECTORY)
    {
        if ((fsatt & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY)) != 0)
        {
            hr = SHELL32_GetCLSIDForDirectory(pfti.szTargetParsingName, L"CLSID", &clsidFolder);

            if (SUCCEEDED(hr))
            {
                /* We got a GUID from a desktop.ini, let's try it */
                hr = SHELL32_BindToSF(m_pidlRoot, &pfti, pidl, &clsidFolder, riid, ppvOut);
                if (SUCCEEDED(hr))
                {
                    TRACE("-- returning (%p) %08x, (%s)\n", *ppvOut, hr, wine_dbgstr_guid(&clsidFolder));
                    return hr;
                }

                /* Something went wrong, re-try it with a normal ShellFSFolder */
                ERR("CFSFolder::BindToObject: %s failed to bind, using fallback (0x%08x)\n", wine_dbgstr_guid(&clsidFolder), hr);
            }
        }
        /* No system folder or the custom class failed */
        clsidFolder = CLSID_ShellFSFolder;
    }
    else
    {
        hr = GetCLSIDForFileType(pidl, L"CLSID", &clsidFolder);
        if (hr == S_FALSE)
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if (hr != S_OK)
            return hr;
    }

    hr = SHELL32_BindToSF(m_pidlRoot, &pfti, pidl, &clsidFolder, riid, ppvOut);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    TRACE ("-- returning (%p) %08x\n", *ppvOut, hr);

    return S_OK;

}

/**************************************************************************
*  CFSFolder::BindToStorage
* PARAMETERS
*  LPCITEMIDLIST pidl,       //[in ] complex pidl to store
*  LPBC          pbc,        //[in ] reserved
*  REFIID        riid,       //[in ] Initial storage interface
*  LPVOID*       ppvObject   //[out] Interface* returned
*/
HRESULT WINAPI CFSFolder::BindToStorage(
    PCUIDLIST_RELATIVE pidl,
    LPBC pbcReserved,
    REFIID riid,
    LPVOID *ppvOut)
{
    FIXME("(%p)->(pidl=%p,%p,%s,%p) stub\n", this, pidl, pbcReserved,
          shdebugstr_guid (&riid), ppvOut);

    *ppvOut = NULL;
    return E_NOTIMPL;
}

/**************************************************************************
*  CFSFolder::CompareIDs
*/

HRESULT WINAPI CFSFolder::CompareIDs(LPARAM lParam,
                                     PCUIDLIST_RELATIVE pidl1,
                                     PCUIDLIST_RELATIVE pidl2)
{
    DWORD shdid1 = _ILItemGetFSItemType(pidl1);
    DWORD shdid2 = _ILItemGetFSItemType(pidl2);
    BOOL bIsFolder1 = shdid1 == SHDID_FS_DIRECTORY;
    BOOL bIsFolder2 = shdid2 == SHDID_FS_DIRECTORY;

    if (!shdid1 || !shdid2 || LOWORD(lParam) >= GENERICSHELLVIEWCOLUMNS)
        return E_INVALIDARG;

    /* When sorting between a File and a Folder, the Folder gets sorted first */
    if (bIsFolder1 != bIsFolder2)
    {
        return MAKE_COMPARE_HRESULT(bIsFolder1 ? -1 : 1);
    }

    UINT64 size1, size2;
    WCHAR buf1[MAX_PATH], buf2[MAX_PATH];
    LPCWSTR s1, s2;
    FileStruct *pfs1 = (FileStruct*)&pidl1->mkid.abID[1];
    FileStruct *pfs2 = (FileStruct*)&pidl2->mkid.abID[1];
    int result;
    switch (LOWORD(lParam))
    {
        case 0: /* Name */
            s1 = GetItemFSName((PITEM)pidl1, buf1, _countof(buf1));
            s1 = GetItemFSName((PITEM)pidl2, buf2, _countof(buf2));
            result = wcsicmp(s1, s2);
            break;
        case 1: /* Type */
            s1 = GetItemExtension((PITEM)pidl1, buf1, _countof(buf1));
            s1 = GetItemExtension((PITEM)pidl2, buf2, _countof(buf2));
            result = wcsicmp(s1 ? s1 : L"", s2 ? s2 : L"");
            break;
        case 2: /* Size */
            if (FAILED(GetItemSizeHelper((PITEM)pidl1, size1)))
                size1 = 0;
            if (FAILED(GetItemSizeHelper((PITEM)pidl2, size2)))
                size2 = 0;
            if (size1 > size2)
                result = 1;
            else if (size1 < size2)
                result = -1;
            else
                result = 0;
            break;
        case 3: /* Modified */
            result = pfs1->uFileDate - pfs2->uFileDate;
            if (result == 0)
                result = pfs1->uFileTime - pfs2->uFileTime;
            break;
        case 4: /* Attributes */
            return SHELL32_CompareDetails(this, lParam, pidl1, pidl2); // FIXME: This is lazy and slow, we can do it much faster right here
        case 5: /* Comments */
            result = 0;
            break;
    }

    if (result == 0)
        return SHELL32_CompareChildren(this, lParam, pidl1, pidl2);

    return MAKE_COMPARE_HRESULT(result);
}

/**************************************************************************
* CFSFolder::CreateViewObject
*/
HRESULT WINAPI CFSFolder::CreateViewObject(HWND hwndOwner,
        REFIID riid, LPVOID * ppvOut)
{
    CComPtr<IShellView> pShellView;
    HRESULT hr = E_INVALIDARG;

    TRACE ("(%p)->(hwnd=%p,%s,%p)\n", this, hwndOwner, shdebugstr_guid (&riid),
           ppvOut);

    if (ppvOut)
    {
        *ppvOut = NULL;

        BOOL bIsDropTarget = IsEqualIID (riid, IID_IDropTarget);
        BOOL bIsShellView = !bIsDropTarget && IsEqualIID (riid, IID_IShellView);

        if (bIsDropTarget || bIsShellView)
        {
            DWORD dwDirAttributes = 0;
            if (PITEM item = IsValidItem(ILFindLastID(m_pidlRoot)))
                dwDirAttributes = GetItemFSAttributes(item);

            if ((dwDirAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY)) != 0)
            {
                CLSID clsidFolder;
                hr = SHELL32_GetCLSIDForDirectory(m_sPathTarget, L"UICLSID", &clsidFolder);
                if (SUCCEEDED(hr))
                {
                    CComPtr<IPersistFolder> spFolder;
                    hr = SHCoCreateInstance(NULL, &clsidFolder, NULL, IID_PPV_ARG(IPersistFolder, &spFolder));
                    if (!FAILED_UNEXPECTEDLY(hr))
                    {
                        hr = spFolder->Initialize(m_pidlRoot);

                        if (!FAILED_UNEXPECTEDLY(hr))
                        {
                            hr = spFolder->QueryInterface(riid, ppvOut);
                        }
                    }
                }
                else
                {
                    // No desktop.ini, or no UICLSID present, continue as if nothing happened
                    hr = E_INVALIDARG;
                }
            }
        }

        if (!SUCCEEDED(hr))
        {
            // No UICLSID handler found, continue to the default handlers
            if (bIsDropTarget)
            {
                hr = CFSDropTarget_CreateInstance(m_sPathTarget, riid, ppvOut);
            }
            else if (IsEqualIID (riid, IID_IContextMenu))
            {
                HKEY hKeys[16];
                UINT cKeys = 0;
                AddClassKeyToArray(L"Directory\\Background", hKeys, &cKeys);

                DEFCONTEXTMENU dcm;
                dcm.hwnd = hwndOwner;
                dcm.pcmcb = this;
                dcm.pidlFolder = m_pidlRoot;
                dcm.psf = this;
                dcm.cidl = 0;
                dcm.apidl = NULL;
                dcm.cKeys = cKeys;
                dcm.aKeys = hKeys;
                dcm.punkAssociationInfo = NULL;
                hr = SHCreateDefaultContextMenu (&dcm, riid, ppvOut);
            }
            else if (bIsShellView)
            {
                SFV_CREATE sfvparams = {sizeof(SFV_CREATE), this, NULL, this};
                hr = SHCreateShellFolderView(&sfvparams, (IShellView**)ppvOut);
            }
            else
            {
                hr = E_INVALIDARG;
            }
        }
    }
    TRACE("-- (%p)->(interface=%p)\n", this, ppvOut);
    return hr;
}

/**************************************************************************
*  CFSFolder::GetAttributesOf
*
* PARAMETERS
*  UINT            cidl,     //[in ] num elements in pidl array
*  LPCITEMIDLIST*  apidl,    //[in ] simple pidl array
*  ULONG*          rgfInOut) //[out] result array
*
*/
HRESULT WINAPI CFSFolder::GetAttributesOf(UINT cidl,
        PCUITEMID_CHILD_ARRAY apidl, DWORD * rgfInOut)
{
    HRESULT hr = S_OK;

    if (!rgfInOut)
        return E_INVALIDARG;
    if (cidl && !apidl)
        return E_INVALIDARG;

    if (*rgfInOut == 0)
        *rgfInOut = ~0;

    if(cidl == 0)
    {
        LPCITEMIDLIST rpidl = ILFindLastID(m_pidlRoot);

        if (IsValidItem(rpidl))
        {
            SHELL32_GetFSItemAttributes(this, rpidl, rgfInOut);
        }
        else if (_ILIsDrive(rpidl)) // FIXME: Why is this valid?
        {
            IShellFolder *psfParent = NULL;
            hr = SHBindToParent(m_pidlRoot, IID_PPV_ARG(IShellFolder, &psfParent), NULL);
            if(SUCCEEDED(hr))
            {
                hr = psfParent->GetAttributesOf(1, &rpidl, (SFGAOF*)rgfInOut);
                psfParent->Release();
            }
        }
        else
        {
            ERR("Got and unknown pidl!\n");
        }
    }
    else
    {
        while (cidl > 0 && *apidl)
        {
            pdump(*apidl);
            if(IsValidItem(*apidl))
                SHELL32_GetFSItemAttributes(this, *apidl, rgfInOut);
            else
                ERR("Got an unknown type of pidl!!!\n");
            apidl++;
            cidl--;
        }
    }
    /* make sure SFGAO_VALIDATE is cleared, some apps depend on that */
    *rgfInOut &= ~SFGAO_VALIDATE;

    TRACE("-- result=0x%08x\n", *rgfInOut);

    return hr;
}

/**************************************************************************
*  CFSFolder::GetUIObjectOf
*
* PARAMETERS
*  HWND           hwndOwner, //[in ] Parent window for any output
*  UINT           cidl,      //[in ] array size
*  LPCITEMIDLIST* apidl,     //[in ] simple pidl array
*  REFIID         riid,      //[in ] Requested Interface
*  UINT*          prgfInOut, //[   ] reserved
*  LPVOID*        ppvObject) //[out] Resulting Interface
*
* NOTES
*  This function gets asked to return "view objects" for one or more (multiple
*  select) items:
*  The viewobject typically is an COM object with one of the following
*  interfaces:
*  IExtractIcon,IDataObject,IContextMenu
*  In order to support icon positions in the default Listview your DataObject
*  must implement the SetData method (in addition to GetData :) - the shell
*  passes a barely documented "Icon positions" structure to SetData when the
*  drag starts, and GetData's it if the drop is in another explorer window that
*  needs the positions.
*/
HRESULT WINAPI CFSFolder::GetUIObjectOf(HWND hwndOwner,
                                        UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                                        REFIID riid, UINT * prgfInOut,
                                        LPVOID * ppvOut)
{
    LPVOID pObj = NULL;
    HRESULT hr = E_INVALIDARG;

    TRACE ("(%p)->(%p,%u,apidl=%p,%s,%p,%p)\n",
           this, hwndOwner, cidl, apidl, shdebugstr_guid (&riid), prgfInOut, ppvOut);

    if (ppvOut)
    {
        *ppvOut = NULL;
        DWORD shdid = cidl ? _ILItemGetFSItemType(apidl[0]) : 0;

        if (cidl == 1 && shdid && shdid != SHDID_FS_DIRECTORY) // FIXME: Why is this not valid for folders?
        {
            hr = _CreateExtensionUIObject(apidl[0], riid, ppvOut);
            if(hr != S_FALSE)
                return hr;
        }

        if (IsEqualIID(riid, IID_IContextMenu) && (cidl >= 1))
        {
            HKEY hKeys[16];
            UINT cKeys = 0;
            AddFSClassKeysToArray(cidl, apidl, hKeys, &cKeys);

            DEFCONTEXTMENU dcm;
            dcm.hwnd = hwndOwner;
            dcm.pcmcb = this;
            dcm.pidlFolder = m_pidlRoot;
            dcm.psf = this;
            dcm.cidl = cidl;
            dcm.apidl = apidl;
            dcm.cKeys = cKeys;
            dcm.aKeys = hKeys;
            dcm.punkAssociationInfo = NULL;
            hr = SHCreateDefaultContextMenu (&dcm, riid, &pObj);
        }
        else if (IsEqualIID (riid, IID_IDataObject))
        {
            if (cidl >= 1)
            {
                hr = IDataObject_Constructor (hwndOwner, m_pidlRoot, apidl, cidl, TRUE, (IDataObject **)&pObj);
            }
            else
            {
                hr = E_INVALIDARG;
            }
        }
        else if ((IsEqualIID (riid, IID_IExtractIconA) || IsEqualIID (riid, IID_IExtractIconW)) && (cidl == 1) && shdid)
        {
            if (shdid != SHDID_FS_DIRECTORY)
                hr = _GetIconHandler(apidl[0], riid, (LPVOID*)&pObj);
            if (hr != S_OK)
                hr = CFSExtractIcon_CreateInstance(this, apidl[0], riid, &pObj);
        }
        else if (IsEqualIID (riid, IID_IDropTarget))
        {
            /* only interested in attempting to bind to shell folders, not files (except exe), so if we fail, rebind to root */
            if (cidl != 1 || FAILED(hr = shdid ? this->_GetDropTarget(apidl[0], (LPVOID*) &pObj) : E_INVALIDARG))
            {
                hr = CFSDropTarget_CreateInstance(m_sPathTarget, riid, (LPVOID*) &pObj);
            }
        }
        else
            hr = E_NOINTERFACE;

        if (SUCCEEDED(hr) && !pObj)
            hr = E_OUTOFMEMORY;

        *ppvOut = pObj;
    }
    TRACE("(%p)->hr=0x%08x\n", this, hr);
    return hr;
}

/******************************************************************************
 * SHELL_FS_HideExtension [Internal]
 *
 * Query the registry if the filename extension of a given path should be
 * hidden.
 *
 * PARAMS
 *  szPath [I] Relative or absolute path of a file
 *
 * RETURNS
 *  TRUE, if the filename's extension should be hidden
 *  FALSE, otherwise.
 */
BOOL SHELL_FS_HideExtension(LPCWSTR szPath)
{
    HKEY hKey;
    DWORD dwData, dwDataSize = sizeof(DWORD);
    BOOL doHide = FALSE; /* The default value is FALSE (win98 at least) */
    LONG lError;

    lError = RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                             0, NULL, 0, KEY_ALL_ACCESS, NULL,
                             &hKey, NULL);
    if (lError == ERROR_SUCCESS)
    {
        lError = RegQueryValueExW(hKey, L"HideFileExt", NULL, NULL, (LPBYTE)&dwData, &dwDataSize);
        if (lError == ERROR_SUCCESS)
            doHide = dwData;
        RegCloseKey(hKey);
    }

    if (!doHide)
    {
        LPCWSTR DotExt = PathFindExtensionW(szPath);
        if (*DotExt != 0)
        {
            WCHAR classname[MAX_PATH];
            LONG classlen = sizeof(classname);
            lError = RegQueryValueW(HKEY_CLASSES_ROOT, DotExt, classname, &classlen);
            if (lError == ERROR_SUCCESS)
            {
                lError = RegOpenKeyW(HKEY_CLASSES_ROOT, classname, &hKey);
                if (lError == ERROR_SUCCESS)
                {
                    lError = RegQueryValueExW(hKey, L"NeverShowExt", NULL, NULL, NULL, NULL);
                    if (lError == ERROR_SUCCESS)
                        doHide = TRUE;

                    RegCloseKey(hKey);
                }
            }
        }
    }

    return doHide;
}

void SHELL_FS_ProcessDisplayFilename(LPWSTR szPath, DWORD dwFlags)
{
    /*FIXME: MSDN also mentions SHGDN_FOREDITING which is not yet handled. */
    if (!(dwFlags & SHGDN_FORPARSING) &&
        ((dwFlags & SHGDN_INFOLDER) || (dwFlags == SHGDN_NORMAL))) {
            if (SHELL_FS_HideExtension(szPath) && szPath[0] != '.')
                PathRemoveExtensionW(szPath);
    }
}

/**************************************************************************
*  CFSFolder::GetDisplayNameOf
*  Retrieves the display name for the specified file object or subfolder
*
* PARAMETERS
*  LPCITEMIDLIST pidl,    //[in ] complex pidl to item
*  DWORD         dwFlags, //[in ] SHGNO formatting flags
*  LPSTRRET      lpName)  //[out] Returned display name
*
* FIXME
*  if the name is in the pidl the ret value should be a STRRET_OFFSET
*/

HRESULT WINAPI CFSFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl,
        DWORD dwFlags, LPSTRRET strRet)
{
    if (!strRet)
        return E_INVALIDARG;

    /* If it is a complex pidl, let the child handle it */
    if (!_ILIsPidlSimple (pidl)) /* complex pidl */
    {
        return SHELL32_GetDisplayNameOfChild(this, pidl, dwFlags, strRet);
    }
    else if (pidl && !pidl->mkid.cb) /* empty pidl */
    {
        /* If it is an empty pidl return only the path of the folder */
        if ((GET_SHGDN_FOR(dwFlags) & SHGDN_FORPARSING) &&
            (GET_SHGDN_RELATION(dwFlags) != SHGDN_INFOLDER) &&
            m_sPathTarget)
        {
            return SHSetStrRet(strRet, m_sPathTarget);
        }
        return E_INVALIDARG;
    }
    DWORD shdid = _ILItemGetFSItemType(pidl);
    if (!shdid)
        return E_INVALIDARG;

    int len = 0;
    LPWSTR pszPath = (LPWSTR)CoTaskMemAlloc((MAX_PATH + 1) * sizeof(WCHAR));
    if (!pszPath)
        return E_OUTOFMEMORY;

    if ((GET_SHGDN_FOR(dwFlags) & SHGDN_FORPARSING) &&
        (GET_SHGDN_RELATION(dwFlags) != SHGDN_INFOLDER) &&
        m_sPathTarget)
    {
        lstrcpynW(pszPath, m_sPathTarget, MAX_PATH);
        PathAddBackslashW(pszPath);
        len = wcslen(pszPath);
    }
    GetItemFSNameToBuffer((PITEM)pidl, pszPath + len, MAX_PATH + 1 - len);
    if (shdid != SHDID_FS_DIRECTORY)
        SHELL_FS_ProcessDisplayFilename(pszPath, dwFlags);

    strRet->uType = STRRET_WSTR;
    strRet->pOleStr = pszPath;

    TRACE ("-- (%p)->(%s)\n", this, strRet->uType == STRRET_CSTR ? strRet->cStr : debugstr_w(strRet->pOleStr));
    return S_OK;
}

/**************************************************************************
*  CFSFolder::SetNameOf
*  Changes the name of a file object or subfolder, possibly changing its item
*  identifier in the process.
*
* PARAMETERS
*  HWND          hwndOwner,  //[in ] Owner window for output
*  LPCITEMIDLIST pidl,       //[in ] simple pidl of item to change
*  LPCOLESTR     lpszName,   //[in ] the items new display name
*  DWORD         dwFlags,    //[in ] SHGNO formatting flags
*  LPITEMIDLIST* ppidlOut)   //[out] simple pidl returned
*/
HRESULT WINAPI CFSFolder::SetNameOf(
    HWND hwndOwner,
    PCUITEMID_CHILD pidl,
    LPCOLESTR lpName,
    DWORD dwFlags,
    PITEMID_CHILD *pPidlOut)
{
    WCHAR szSrc[MAX_PATH + 1], szDest[MAX_PATH + 1];
    DWORD shdid = _ILItemGetFSItemType(pidl);
    if (!shdid)
        return E_INVALIDARG;
    BOOL bIsFolder = shdid == SHDID_FS_DIRECTORY;

    TRACE ("(%p)->(%p,pidl=%p,%s,%u,%p)\n", this, hwndOwner, pidl,
           debugstr_w (lpName), dwFlags, pPidlOut);

    WCHAR itemnamebuf[MAX_PATH];
    LPCWSTR itemname = GetItemFSName((PITEM)pidl, itemnamebuf, _countof(itemnamebuf));

    /* build source path */
    PathCombineW(szSrc, m_sPathTarget, itemname);

    /* build destination path */
    if (dwFlags == SHGDN_NORMAL || dwFlags & SHGDN_INFOLDER)
        PathCombineW(szDest, m_sPathTarget, lpName);
    else
        lstrcpynW(szDest, lpName, MAX_PATH);

    if(!(dwFlags & SHGDN_FORPARSING) && SHELL_FS_HideExtension(szSrc)) {
        WCHAR *ext = PathFindExtensionW(szSrc);
        if(*ext != '\0') {
            INT len = wcslen(szDest);
            lstrcpynW(szDest + len, ext, MAX_PATH - len);
        }
    }

    TRACE ("src=%s dest=%s\n", debugstr_w(szSrc), debugstr_w(szDest));
    if (!wcscmp(szSrc, szDest))
    {
        /* src and destination is the same */
        HRESULT hr = S_OK;
        if (pPidlOut)
            hr = _ILCreateFromPathW(szDest, pPidlOut);

        return hr;
    }

    if (MoveFileW (szSrc, szDest))
    {
        HRESULT hr = S_OK;

        if (pPidlOut)
            hr = _ILCreateFromPathW(szDest, pPidlOut);

        SHChangeNotify (bIsFolder ? SHCNE_RENAMEFOLDER : SHCNE_RENAMEITEM,
                        SHCNF_PATHW, szSrc, szDest);

        return hr;
    }

    return E_FAIL;
}

HRESULT WINAPI CFSFolder::GetDefaultSearchGUID(GUID * pguid)
{
    FIXME ("(%p)\n", this);
    return E_NOTIMPL;
}

HRESULT WINAPI CFSFolder::EnumSearches(IEnumExtraSearch ** ppenum)
{
    FIXME ("(%p)\n", this);
    return E_NOTIMPL;
}

HRESULT WINAPI CFSFolder::GetDefaultColumn(DWORD dwRes,
        ULONG * pSort, ULONG * pDisplay)
{
    TRACE ("(%p)\n", this);

    if (pSort)
        *pSort = 0;
    if (pDisplay)
        *pDisplay = 0;

    return S_OK;
}

HRESULT WINAPI CFSFolder::GetDefaultColumnState(UINT iColumn,
        DWORD * pcsFlags)
{
    TRACE ("(%p)\n", this);

    if (!pcsFlags || iColumn >= GENERICSHELLVIEWCOLUMNS)
        return E_INVALIDARG;

    *pcsFlags = GenericSFHeader[iColumn].pcsFlags;

    return S_OK;
}

HRESULT WINAPI CFSFolder::GetDetailsEx(PCUITEMID_CHILD pidl,
                                       const SHCOLUMNID * pscid, VARIANT * pv)
{
    if (!IsValidItem(pidl) || !pscid || !pv)
    {
        return E_INVALIDARG;
    }
    else if (IsEqualPropertyKey(*pscid, PKEY_FindData))
    {
        WIN32_FIND_DATAW fd;
        _GetFindData((PITEM)pidl, fd);
        return SHELL_InitVariantFromBuffer(&fd, sizeof(fd), pv);
    }
    else if (IsEqualPropertyKey(*pscid, PKEY_DescriptionID))
    {
        SHDESCRIPTIONID shdi;
        shdi.dwDescriptionId = _ILItemGetFSItemType(pidl);
        if (FAILED(GetItemCLSID((PITEM)pidl, shdi.clsid)))
            shdi.clsid = GUID_NULL;
        return SHELL_InitVariantFromBuffer(&shdi, sizeof(shdi), pv);
    }
    /*
    TODO: else if (!MapSCIDToColumn(*pscid, col))
    {
        return E_NOTIMPL;
    }
    else switch(col)
    */
    FIXME ("(%p)\n", this);
    return E_NOTIMPL;
}

HRESULT WINAPI CFSFolder::GetDetailsOf(PCUITEMID_CHILD pidl,
                                       UINT iColumn, SHELLDETAILS * psd)
{
    HRESULT hr = E_FAIL;

    TRACE ("(%p)->(%p %i %p)\n", this, pidl, iColumn, psd);

    if (!psd || iColumn >= GENERICSHELLVIEWCOLUMNS)
        return E_INVALIDARG;

    if (!pidl)
    {
        /* the header titles */
        psd->fmt = GenericSFHeader[iColumn].fmt;
        psd->cxChar = GenericSFHeader[iColumn].cxChar;
        return SHSetStrRet(&psd->str, GenericSFHeader[iColumn].colnameid);
    }
    else
    {
        hr = S_OK;
        psd->str.uType = STRRET_CSTR;
        /* the data from the pidl */
        switch (iColumn)
        {
            case 0:                /* name */
                hr = GetDisplayNameOf (pidl, SHGDN_NORMAL | SHGDN_INFOLDER, &psd->str);
                break;
            case 1:                /* type */
                _ILGetFileType(pidl, psd->str.cStr, MAX_PATH); // FIXME
                break;
            case 2:                /* size */
                _ILGetFileSize(pidl, psd->str.cStr, MAX_PATH); // FIXME
                break;
            case 3:                /* date */
                _ILGetFileDate(pidl, psd->str.cStr, MAX_PATH); // FIXME
                break;
            case 4:                /* attributes */
                _ILGetFileAttributes(pidl, psd->str.cStr, MAX_PATH); // FIXME
                break;
            case 5:                /* FIXME: comments */
                psd->str.cStr[0] = 0;
                break;
        }
    }

    return hr;
}

HRESULT WINAPI CFSFolder::MapColumnToSCID (UINT column,
        SHCOLUMNID * pscid)
{
    FIXME ("(%p)\n", this);
    return E_NOTIMPL;
}

/************************************************************************
 * CFSFolder::GetClassID
 */
HRESULT WINAPI CFSFolder::GetClassID(CLSID * lpClassId)
{
    TRACE ("(%p)\n", this);

    if (!lpClassId)
        return E_POINTER;

    *lpClassId = *m_pclsid;

    return S_OK;
}

/************************************************************************
 * CFSFolder::Initialize
 *
 * NOTES
 *  m_sPathTarget is not set. Don't know how to handle in a non rooted environment.
 */
HRESULT WINAPI CFSFolder::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    WCHAR wszTemp[MAX_PATH];

    TRACE ("(%p)->(%p)\n", this, pidl);

    SHFree(m_pidlRoot);     /* free the old pidl */
    m_pidlRoot = ILClone (pidl); /* set my pidl */

    SHFree (m_sPathTarget);
    m_sPathTarget = NULL;

    /* set my path */
    if (SHGetPathFromIDListW (pidl, wszTemp))
    {
        int len = wcslen(wszTemp);
        m_sPathTarget = (WCHAR *)SHAlloc((len + 1) * sizeof(WCHAR));
        if (!m_sPathTarget)
            return E_OUTOFMEMORY;
        memcpy(m_sPathTarget, wszTemp, (len + 1) * sizeof(WCHAR));
    }

    TRACE ("--(%p)->(%s)\n", this, debugstr_w(m_sPathTarget));
    return S_OK;
}

/**************************************************************************
 * CFSFolder::GetCurFolder
 */
HRESULT WINAPI CFSFolder::GetCurFolder(PIDLIST_ABSOLUTE * pidl)
{
    TRACE ("(%p)->(%p)\n", this, pidl);

    if (!pidl)
        return E_POINTER;

    *pidl = ILClone(m_pidlRoot);
    return S_OK;
}

/**************************************************************************
 * CFSFolder::InitializeEx
 *
 * FIXME: error handling
 */
HRESULT WINAPI CFSFolder::InitializeEx(IBindCtx * pbc, LPCITEMIDLIST pidlRootx,
                                       const PERSIST_FOLDER_TARGET_INFO * ppfti)
{
    WCHAR wszTemp[MAX_PATH];

    TRACE("(%p)->(%p,%p,%p)\n", this, pbc, pidlRootx, ppfti);
    if (ppfti)
        TRACE("--%p %s %s 0x%08x 0x%08x\n",
              ppfti->pidlTargetFolder, debugstr_w (ppfti->szTargetParsingName),
              debugstr_w (ppfti->szNetworkProvider), ppfti->dwAttributes,
              ppfti->csidl);

    pdump (pidlRootx);
    if (ppfti && ppfti->pidlTargetFolder)
        pdump(ppfti->pidlTargetFolder);

    if (m_pidlRoot)
        __SHFreeAndNil(&m_pidlRoot);    /* free the old */
    if (m_sPathTarget)
        __SHFreeAndNil(&m_sPathTarget);

    /*
     * Root path and pidl
     */
    m_pidlRoot = ILClone(pidlRootx);

    /*
     *  the target folder is spezified in csidl OR pidlTargetFolder OR
     *  szTargetParsingName
     */
    if (ppfti)
    {
        if (ppfti->csidl != -1)
        {
            if (SHGetSpecialFolderPathW(0, wszTemp, ppfti->csidl,
                                        ppfti->csidl & CSIDL_FLAG_CREATE)) {
                int len = wcslen(wszTemp);
                m_sPathTarget = (WCHAR *)SHAlloc((len + 1) * sizeof(WCHAR));
                if (!m_sPathTarget)
                    return E_OUTOFMEMORY;
                memcpy(m_sPathTarget, wszTemp, (len + 1) * sizeof(WCHAR));
            }
        }
        else if (ppfti->szTargetParsingName[0])
        {
            int len = wcslen(ppfti->szTargetParsingName);
            m_sPathTarget = (WCHAR *)SHAlloc((len + 1) * sizeof(WCHAR));
            if (!m_sPathTarget)
                return E_OUTOFMEMORY;
            memcpy(m_sPathTarget, ppfti->szTargetParsingName,
                   (len + 1) * sizeof(WCHAR));
        }
        else if (ppfti->pidlTargetFolder)
        {
            if (SHGetPathFromIDListW(ppfti->pidlTargetFolder, wszTemp))
            {
                int len = wcslen(wszTemp);
                m_sPathTarget = (WCHAR *)SHAlloc((len + 1) * sizeof(WCHAR));
                if (!m_sPathTarget)
                    return E_OUTOFMEMORY;
                memcpy(m_sPathTarget, wszTemp, (len + 1) * sizeof(WCHAR));
            }
        }
    }

    TRACE("--(%p)->(target=%s)\n", this, debugstr_w(m_sPathTarget));
    pdump(m_pidlRoot);
    return (m_sPathTarget) ? S_OK : E_FAIL;
}

HRESULT WINAPI CFSFolder::GetFolderTargetInfo(PERSIST_FOLDER_TARGET_INFO * ppfti)
{
    FIXME("(%p)->(%p)\n", this, ppfti);
    ZeroMemory(ppfti, sizeof (*ppfti));
    return E_NOTIMPL;
}

HRESULT CFSFolder::_CreateExtensionUIObject(PCUIDLIST_RELATIVE pidl, REFIID riid, LPVOID *ppvOut)
{
    WCHAR buf[MAX_PATH];

    sprintfW(buf, L"ShellEx\\{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             riid.Data1, riid.Data2, riid.Data3,
             riid.Data4[0], riid.Data4[1], riid.Data4[2], riid.Data4[3],
             riid.Data4[4], riid.Data4[5], riid.Data4[6], riid.Data4[7]);

    CLSID clsid;
    HRESULT hr;

    hr = GetCLSIDForFileType(pidl, buf, &clsid);
    if (hr != S_OK)
        return hr;

    hr = _CreateShellExtInstance(&clsid, pidl, riid, ppvOut);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    return S_OK;
}

HRESULT CFSFolder::_GetDropTarget(LPCITEMIDLIST pidl, LPVOID *ppvOut)
{
    HRESULT hr;

    TRACE("CFSFolder::_GetDropTarget entered\n");

    if (_ILIsFolder (pidl))
    {
        CComPtr<IShellFolder> psfChild;
        hr = this->BindToObject(pidl, NULL, IID_PPV_ARG(IShellFolder, &psfChild));
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        return psfChild->CreateViewObject(NULL, IID_IDropTarget, ppvOut);
    }

    CLSID clsid;
    hr = GetCLSIDForFileType(pidl, L"shellex\\DropHandler", &clsid);
    if (hr != S_OK)
        return hr;

    hr = _CreateShellExtInstance(&clsid, pidl, IID_IDropTarget, ppvOut);
    if (FAILED_UNEXPECTEDLY(hr))
        return S_FALSE;

    return S_OK;
}

HRESULT CFSFolder::_GetIconHandler(LPCITEMIDLIST pidl, REFIID riid, LPVOID *ppvOut)
{
    CLSID clsid;
    HRESULT hr;

    hr = GetCLSIDForFileType(pidl, L"shellex\\IconHandler", &clsid);
    if (hr != S_OK)
        return hr;

    hr = _CreateShellExtInstance(&clsid, pidl, riid, ppvOut);
    if (FAILED_UNEXPECTEDLY(hr))
        return S_FALSE;

    return S_OK;
}

HRESULT CFSFolder::_CreateShellExtInstance(const CLSID *pclsid, LPCITEMIDLIST pidl, REFIID riid, LPVOID *ppvOut)
{
    HRESULT hr;
    WCHAR wszPath[MAX_PATH];

    FileStructW* pDataW = _ILGetFileStructW(pidl);
    if (!pDataW)
    {
        ERR("Got garbage pidl\n");
        pdump_always(pidl);
        return E_INVALIDARG;
    }

    PathCombineW(wszPath, m_sPathTarget, pDataW->wszName);

    CComPtr<IPersistFile> pp;
    hr = SHCoCreateInstance(NULL, pclsid, NULL, IID_PPV_ARG(IPersistFile, &pp));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    pp->Load(wszPath, 0);

    hr = pp->QueryInterface(riid, ppvOut);
    if (hr != S_OK)
    {
        ERR("Failed to query for interface IID_IShellExtInit hr %x pclsid %s\n", hr, wine_dbgstr_guid(pclsid));
        return hr;
    }
    return hr;
}

HRESULT WINAPI CFSFolder::CallBack(IShellFolder *psf, HWND hwndOwner, IDataObject *pdtobj, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg != DFM_MERGECONTEXTMENU && uMsg != DFM_INVOKECOMMAND)
        return S_OK;

    /* no data object means no selection */
    if (!pdtobj)
    {
        if (uMsg == DFM_INVOKECOMMAND && wParam == 0)
        {
            // Create an data object
            CComHeapPtr<ITEMID_CHILD> pidlChild(ILClone(ILFindLastID(m_pidlRoot)));
            CComHeapPtr<ITEMIDLIST> pidlParent(ILClone(m_pidlRoot));
            ILRemoveLastID(pidlParent);

            CComPtr<IDataObject> pDataObj;
            HRESULT hr = SHCreateDataObject(pidlParent, 1, &pidlChild, NULL, IID_PPV_ARG(IDataObject, &pDataObj));
            if (!FAILED_UNEXPECTEDLY(hr))
            {
                // Ask for a title to display
                CComHeapPtr<WCHAR> wszName;
                if (!FAILED_UNEXPECTEDLY(SHGetNameFromIDList(m_pidlRoot, SIGDN_PARENTRELATIVEPARSING, &wszName)))
                {
                    BOOL bSuccess = SH_ShowPropertiesDialog(wszName, pDataObj);
                    if (!bSuccess)
                        ERR("SH_ShowPropertiesDialog failed\n");
                }
            }
        }
        else if (uMsg == DFM_MERGECONTEXTMENU)
        {
            QCMINFO *pqcminfo = (QCMINFO *)lParam;
            HMENU hpopup = CreatePopupMenu();
            _InsertMenuItemW(hpopup, 0, TRUE, 0, MFT_STRING, MAKEINTRESOURCEW(IDS_PROPERTIES), MFS_ENABLED);
            Shell_MergeMenus(pqcminfo->hmenu, hpopup, pqcminfo->indexMenu++, pqcminfo->idCmdFirst, pqcminfo->idCmdLast, MM_ADDSEPARATOR);
            DestroyMenu(hpopup);
        }

        return S_OK;
    }

    if (uMsg != DFM_INVOKECOMMAND || wParam != DFM_CMD_PROPERTIES)
        return S_OK;

    return Shell_DefaultContextMenuCallBack(this, pdtobj);
}

static HBITMAP DoLoadPicture(LPCWSTR pszFileName)
{
    // create stream from file
    HRESULT hr;
    CComPtr<IStream> pStream;
    hr = SHCreateStreamOnFileEx(pszFileName, STGM_READ, FILE_ATTRIBUTE_NORMAL,
                                FALSE, NULL, &pStream);
    if (FAILED(hr))
        return NULL;

    // load the picture
    HBITMAP hbm = NULL;
    CComPtr<IPicture> pPicture;
    OleLoadPicture(pStream, 0, FALSE, IID_IPicture, (LPVOID *)&pPicture);

    // get the bitmap handle
    if (pPicture)
    {
        pPicture->get_Handle((OLE_HANDLE *)&hbm);

        // copy the bitmap handle
        hbm = (HBITMAP)CopyImage(hbm, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    }

    return hbm;
}

HRESULT WINAPI CFSFolder::GetCustomViewInfo(ULONG unknown, SFVM_CUSTOMVIEWINFO_DATA *data)
{
    if (data == NULL)
    {
        return E_POINTER;
    }
    if (data->cbSize != sizeof(*data))
    {
        // NOTE: You have to set the cbData member before SFVM_GET_CUSTOMVIEWINFO call.
        return E_INVALIDARG;
    }

    data->hbmBack = NULL;
    data->clrText = CLR_INVALID;
    data->clrTextBack = CLR_INVALID;

    WCHAR szPath[MAX_PATH], szIniFile[MAX_PATH];

    // does the folder exists?
    if (!SHGetPathFromIDListW(m_pidlRoot, szPath) || !PathIsDirectoryW(szPath))
    {
        return E_INVALIDARG;
    }

    // don't use custom view in network path for security
    if (PathIsNetworkPath(szPath))
    {
        return E_ACCESSDENIED;
    }

    // build the ini file path
    StringCchCopyW(szIniFile, _countof(szIniFile), szPath);
    PathAppend(szIniFile, L"desktop.ini");

    static LPCWSTR TheGUID = L"{BE098140-A513-11D0-A3A4-00C04FD706EC}";
    static LPCWSTR Space = L" \t\n\r\f\v";

    // get info from ini file
    WCHAR szImage[MAX_PATH], szText[64];

    // load the image
    szImage[0] = UNICODE_NULL;
    GetPrivateProfileStringW(TheGUID, L"IconArea_Image", L"", szImage, _countof(szImage), szIniFile);
    if (szImage[0])
    {
        StrTrimW(szImage, Space);
        if (PathIsRelativeW(szImage))
        {
            PathAppendW(szPath, szImage);
            StringCchCopyW(szImage, _countof(szImage), szPath);
        }
        data->hbmBack = DoLoadPicture(szImage);
    }

    // load the text color
    szText[0] = UNICODE_NULL;
    GetPrivateProfileStringW(TheGUID, L"IconArea_Text", L"", szText, _countof(szText), szIniFile);
    if (szText[0])
    {
        StrTrimW(szText, Space);

        LPWSTR pchEnd = NULL;
        COLORREF cr = (wcstol(szText, &pchEnd, 0) & 0xFFFFFF);

        if (pchEnd && !*pchEnd)
            data->clrText = cr;
    }

    // load the text background color
    szText[0] = UNICODE_NULL;
    GetPrivateProfileStringW(TheGUID, L"IconArea_TextBackground", L"", szText, _countof(szText), szIniFile);
    if (szText[0])
    {
        StrTrimW(szText, Space);

        LPWSTR pchEnd = NULL;
        COLORREF cr = (wcstol(szText, &pchEnd, 0) & 0xFFFFFF);

        if (pchEnd && !*pchEnd)
            data->clrTextBack = cr;
    }

    if (data->hbmBack != NULL || data->clrText != CLR_INVALID || data->clrTextBack != CLR_INVALID)
        return S_OK;

    return E_FAIL;
}

HRESULT WINAPI CFSFolder::MessageSFVCB(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HRESULT hr = E_NOTIMPL;
    switch (uMsg)
    {
    case SFVM_GET_CUSTOMVIEWINFO:
        hr = GetCustomViewInfo((ULONG)wParam, (SFVM_CUSTOMVIEWINFO_DATA *)lParam);
        break;
    }
    return hr;
}
