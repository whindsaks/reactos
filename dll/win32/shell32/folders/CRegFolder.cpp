/*
 * ReactOS Shell
 *
 * Copyright 2016 Giannis Adamopoulos
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * https://learn.microsoft.com/en-us/windows/win32/shell/nse-implement#registering-an-extension
 * lists WantsFORPARSING, HideFolderVerbs, HideAsDelete, HideAsDeletePerUser,
 * QueryForOverlay and InfoTip as values we should handle.
 */

#include <precomp.h>

WINE_DEFAULT_DEBUG_CHANNEL (shell);

#define REGFOLDERITEMSFGAOMASK (SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_HASPROPSHEET | \
                                SFGAO_FILESYSTEM | SFGAO_FILESYSANCESTOR | SFGAO_BROWSABLE | \
                                SFGAO_STORAGE | SFGAO_STORAGEANCESTOR | SFGAO_DROPTARGET)

HRESULT SHELL32_GetCLSIDKeyRegValue(REFGUID guid, LPCWSTR SubKey, LPCWSTR Name, LPDWORD Type = NULL, LPDWORD Size = NULL, void *Data = NULL)
{
    // Like HCR_RegOpenClassIDKey + SHGetValueW but without the overhead of opening two registry keys
    WCHAR guidstr[42], key[MAX_PATH];
    StringFromGUID2(guid, guidstr, 42);
    if (!SubKey)
        SubKey = L"";
    HRESULT hr = StringCchPrintfW(key, _countof(key), L"CLSID\\%s%s%s",
                                  guidstr, *SubKey ? L"\\" : L"", SubKey);
    if (SUCCEEDED(hr))
    {
        LONG r = SHGetValueW(HKEY_CLASSES_ROOT, key, Name, Type, Data, Size);
        hr = HRESULT_FROM_WIN32(r);
    }
    return hr;
}

static BOOL CLSIDKeyHasRegValue(REFGUID guid, LPCWSTR SubKey, LPCWSTR Name)
{
    return SUCCEEDED(SHELL32_GetCLSIDKeyRegValue(guid, SubKey, Name));
}

static BOOL IsFolder(IShellFolder *psf, PCUITEMID_CHILD item)
{
    UINT assocf = SHELL32_AssocGetBaseClassInitFlags(psf, item);
    return assocf & ASSOCF_INIT_DEFAULTTOFOLDER;
}

#if 0
HRESULT CALLBACK RegFolderContextMenuCallback(IShellFolder *psf,
                                              HWND         hwnd,
                                              IDataObject  *pdtobj,
                                              UINT         uMsg,
                                              WPARAM       wParam,
                                              LPARAM       lParam)
{
    if (uMsg != DFM_INVOKECOMMAND || wParam != DFM_CMD_PROPERTIES)
        return S_OK;

    PIDLIST_ABSOLUTE pidlFolder;
    PUITEMID_CHILD *apidl;
    UINT cidl;
    HRESULT hr = SH_GetApidlFromDataObject(pdtobj, &pidlFolder, &apidl, &cidl);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    if (_ILIsMyComputer(apidl[0]))
    {
        if (32 >= (UINT_PTR)ShellExecuteW(hwnd,
                                          L"open",
                                          L"rundll32.exe",
                                          L"shell32.dll,Control_RunDLL sysdm.cpl",
                                          NULL,
                                          SW_SHOWNORMAL))
        {
            hr = E_FAIL;
        }
    }
    else if (_ILIsDesktop(apidl[0]))
    {
        if (32 >= (UINT_PTR)ShellExecuteW(hwnd,
                                          L"open",
                                          L"rundll32.exe",
                                          L"shell32.dll,Control_RunDLL desk.cpl",
                                          NULL,
                                          SW_SHOWNORMAL))
        {
            hr = E_FAIL;
        }
    }
    else if (_ILIsNetHood(apidl[0]))
    {
        // FIXME path!
        if (32 >= (UINT_PTR)ShellExecuteW(NULL,
                                          L"open",
                                          L"explorer.exe",
                                          L"::{7007ACC7-3202-11D1-AAD2-00805FC1270E}",
                                          NULL,
                                          SW_SHOWDEFAULT))
        {
            hr = E_FAIL;
        }
    }
    else if (_ILIsBitBucket(apidl[0]))
    {
        /* FIXME: detect the drive path of bitbucket if appropiate */
        if (!SH_ShowRecycleBinProperties(L'C'))
            hr = E_FAIL;
    }
    else
    {
        /* Tell the caller to run the default action */
        hr = S_FALSE;
    }

    SHFree(pidlFolder);
    _ILFreeaPidl(apidl, cidl);

    return hr;
}
#endif

HRESULT CGuidItemContextMenu_CreateInstance(PCIDLIST_ABSOLUTE pidlFolder,
                                            HWND hwnd,
                                            UINT cidl,
                                            PCUITEMID_CHILD_ARRAY apidl,
                                            IShellFolder *psf,
                                            IContextMenuCB *pcmcb,
                                            IContextMenu **ppcm)
{
    HKEY hKeys[10];
    UINT cKeys = 0;

    // ASSOCKEY_CLASS
    GUID const * const pGuid = _ILGetGUIDPointer(apidl[0]);
    if (pGuid)
    {
        LPOLESTR pwszCLSID;
        WCHAR key[60];

        wcscpy(key, L"CLSID\\");
        HRESULT hr = StringFromCLSID(*pGuid, &pwszCLSID);
        if (hr == S_OK)
        {
            wcscpy(&key[6], pwszCLSID);
            AddClassKeyToArray(key, hKeys, &cKeys);
        }
    }

    // ASSOCKEY_BASECLASS
    // Only add the folder key for folder items because the "Folder Options" item
    // in Control Panel should not have an Explore verb.
    if (IsFolder(psf, apidl[0]))
    {
        if (!pGuid || !CLSIDKeyHasRegValue(*pGuid, L"ShellFolder", L"HideFolderVerbs"))
            AddClassKeyToArray(L"Folder", hKeys, &cKeys);
    }

    DEFCONTEXTMENU dcm { hwnd, pcmcb, pidlFolder, psf, cidl, apidl, NULL, cKeys, hKeys };
    return SHCreateDefaultContextMenu(&dcm, IID_PPV_ARG(IContextMenu, ppcm));
}

HRESULT CGuidItemExtractIcon_CreateInstance(PCREGFOLDERITEMATTRIBUTES pItem, LPCITEMIDLIST pidl, REFIID iid, LPVOID * ppvOut)
{
    CComPtr<IDefaultExtractIconInit>    initIcon;
    HRESULT hr;
    GUID const * riid;
    int icon_idx;
    WCHAR wTemp[MAX_PATH];

    hr = SHCreateDefaultExtractIcon(IID_PPV_ARG(IDefaultExtractIconInit,&initIcon));
    if (FAILED(hr))
        return hr;

    if (_ILIsDesktop(pidl))
    {
        initIcon->SetNormalIcon(swShell32Name, -IDI_SHELL_DESKTOP);
        return initIcon->QueryInterface(iid, ppvOut);
    }

    riid = _ILGetGUIDPointer(pidl);
    if (!riid)
        return E_FAIL;

    /* my computer and other shell extensions */
    WCHAR xriid[50];

    swprintf(xriid, L"CLSID\\{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             riid->Data1, riid->Data2, riid->Data3,
             riid->Data4[0], riid->Data4[1], riid->Data4[2], riid->Data4[3],
             riid->Data4[4], riid->Data4[5], riid->Data4[6], riid->Data4[7]);

    const WCHAR* iconname = NULL;
    if (_ILIsBitBucket(pidl))
    {
        CComPtr<IEnumIDList> EnumIDList;
        CoInitialize(NULL);

        CComPtr<IShellFolder2> psfRecycleBin;
        CComPtr<IShellFolder> psfDesktop;
        hr = SHGetDesktopFolder(&psfDesktop);

        if (SUCCEEDED(hr))
            hr = psfDesktop->BindToObject(pidl, NULL, IID_PPV_ARG(IShellFolder2, &psfRecycleBin));
        if (SUCCEEDED(hr))
            hr = psfRecycleBin->EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &EnumIDList);

        ULONG itemcount;
        LPITEMIDLIST pidl = NULL;
        if (SUCCEEDED(hr) && (hr = EnumIDList->Next(1, &pidl, &itemcount)) == S_OK)
        {
            CoTaskMemFree(pidl);
            iconname = L"Full";
        } else {
            iconname = L"Empty";
        }
    }

    if (HCR_GetIconW(xriid, wTemp, iconname, MAX_PATH, &icon_idx))
    {
        initIcon->SetNormalIcon(wTemp, icon_idx);
    }
    else
    {
        BOOL success = FALSE;
        if (pItem && pItem->icon)
        {
            LPWSTR path = swShell32Name;
            int index = -(int)LOWORD(pItem->icon);
#if 0 // Not needed yet, everyone uses shell32 so far
            WCHAR buf[MAX_PATH];
            if (!IS_INTRESOURCE(pItem->icon))
            {
                STRRET *pSR = (STRRET*)buf; // Yep, this is a union with the output buffer
                pSR->uType = STRRET_OFFSET;
                pSR->u.uOffset = 0;
                StrRetToStrNW(buf, _countof(buf), pSR, (LPCITEMIDLIST)pItem->icon);
                index = PathParseIconLocationW(path = buf);
            }
#endif
            success = SUCCEEDED(initIcon->SetNormalIcon(path, index));
        }
        if (!success)
            initIcon->SetNormalIcon(swShell32Name, -IDI_SHELL_FOLDER);
    }

    return initIcon->QueryInterface(iid, ppvOut);
}

class CRegFolderEnum :
    public CEnumIDListBase
{
    public:
        CRegFolderEnum();
        ~CRegFolderEnum();
        HRESULT Initialize(IShellFolder*psf, LPCWSTR lpszEnumKeyName, DWORD contf);
        HRESULT AddItemsFromKey(IShellFolder*psf, HKEY hkey_root, LPCWSTR szRepPath, DWORD contf);

        BEGIN_COM_MAP(CRegFolderEnum)
        COM_INTERFACE_ENTRY_IID(IID_IEnumIDList, IEnumIDList)
        END_COM_MAP()
};

enum registry_columns
{
    REGISTRY_COL_NAME,
    REGISTRY_COL_TYPE,
    REGISTRY_COL_VALUE,
    REGISTRY_COL_COUNT,
};

CRegFolderEnum::CRegFolderEnum()
{
}

CRegFolderEnum::~CRegFolderEnum()
{
}

HRESULT CRegFolderEnum::Initialize(IShellFolder*psf, LPCWSTR lpszEnumKeyName, DWORD contf)
{
    WCHAR KeyName[MAX_PATH];

    if (!(contf & SHCONTF_FOLDERS))
        return S_OK;

    HRESULT hr = StringCchPrintfW(KeyName, MAX_PATH,
                                  L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\%s\\Namespace",
                                  lpszEnumKeyName);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    AddItemsFromKey(psf, HKEY_LOCAL_MACHINE, KeyName, contf);
    AddItemsFromKey(psf, HKEY_CURRENT_USER, KeyName, contf);

    return S_OK;
}

HRESULT CRegFolderEnum::AddItemsFromKey(IShellFolder*psf, HKEY hkey_root, LPCWSTR szRepPath, DWORD contf)
{
    WCHAR name[MAX_PATH];
    HKEY hkey;

    if (RegOpenKeyW(hkey_root, szRepPath, &hkey) != ERROR_SUCCESS)
        return S_FALSE;

    for (int idx = 0; ; idx++)
    {
        if (RegEnumKeyW(hkey, idx, name, MAX_PATH) != ERROR_SUCCESS)
            break;

        /* If the name of the key is not a guid try to get the default value of the key */
        if (name[0] != L'{')
        {
            DWORD dwSize = sizeof(name);
            RegGetValueW(hkey, name, NULL, RRF_RT_REG_SZ, NULL, name, &dwSize);
        }

        if (*name == '{')
        {
            LPITEMIDLIST pidl = _ILCreateGuidFromStrW(name);
            if (!pidl)
            {
                TRACE("CRegFolderEnum failed to create item\n");
                continue;
            }

            enum { CONTMASK = SHCONTF_INCLUDEHIDDEN };
            if (SHELL_FolderEnumIncludeItem<CONTMASK>(psf, pidl, contf) == S_OK)
                AddToEnumList(pidl);
            else
                SHFree(pidl);
        }
    }

    RegCloseKey(hkey);

    return S_OK;
}

class CRegFolder :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellFolder2, public IContextMenuCB
{
    private:
        GUID m_guid;
        CAtlStringW m_rootPath;
        CAtlStringW m_enumKeyName;
        CComHeapPtr<ITEMIDLIST> m_pidlRoot;
        PCREGFOLDERITEMATTRIBUTES m_ItemAttributes;
        UINT m_ItemAttributeCount;

        PCREGFOLDERITEMATTRIBUTES GetStaticItem(REFCLSID clsid);
        PCREGFOLDERITEMATTRIBUTES GetStaticItem(LPCITEMIDLIST pidl);
        HRESULT GetGuidItemAttributes(LPCITEMIDLIST pidl, LPDWORD pdwAttributes);
    public:
        CRegFolder();
        ~CRegFolder();
        HRESULT WINAPI Initialize(const REGFOLDERCREATEPARAMETERS *p);

        // IShellFolder
        virtual HRESULT WINAPI ParseDisplayName(HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName, ULONG *pchEaten, PIDLIST_RELATIVE *ppidl, ULONG *pdwAttributes);
        virtual HRESULT WINAPI EnumObjects(HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST *ppEnumIDList);
        virtual HRESULT WINAPI BindToObject(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut);
        virtual HRESULT WINAPI BindToStorage(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut);
        virtual HRESULT WINAPI CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2);
        virtual HRESULT WINAPI CreateViewObject(HWND hwndOwner, REFIID riid, LPVOID *ppvOut);
        virtual HRESULT WINAPI GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD *rgfInOut);
        virtual HRESULT WINAPI GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT * prgfInOut, LPVOID * ppvOut);
        virtual HRESULT WINAPI GetDisplayNameOf(PCUITEMID_CHILD pidl, DWORD dwFlags, LPSTRRET strRet);
        virtual HRESULT WINAPI SetNameOf(HWND hwndOwner, PCUITEMID_CHILD pidl, LPCOLESTR lpName, DWORD dwFlags, PITEMID_CHILD *pPidlOut);

        /* ShellFolder2 */
        virtual HRESULT WINAPI GetDefaultSearchGUID(GUID *pguid);
        virtual HRESULT WINAPI EnumSearches(IEnumExtraSearch **ppenum);
        virtual HRESULT WINAPI GetDefaultColumn(DWORD dwRes, ULONG *pSort, ULONG *pDisplay);
        virtual HRESULT WINAPI GetDefaultColumnState(UINT iColumn, DWORD *pcsFlags);
        virtual HRESULT WINAPI GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv);
        virtual HRESULT WINAPI GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd);
        virtual HRESULT WINAPI MapColumnToSCID(UINT column, SHCOLUMNID *pscid);

        /* IContextMenuCB */
        virtual HRESULT WINAPI CallBack(IShellFolder *psf, HWND hOwner, IDataObject *pdo, UINT uMsg, WPARAM wParam, LPARAM lParam);

        DECLARE_NOT_AGGREGATABLE(CRegFolder)

        DECLARE_PROTECT_FINAL_CONSTRUCT()

        BEGIN_COM_MAP(CRegFolder)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder2, IShellFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder, IShellFolder)
        COM_INTERFACE_ENTRY_IID(IID_IContextMenuCB, IContextMenuCB)
        END_COM_MAP()
};

CRegFolder::CRegFolder()
{
}

CRegFolder::~CRegFolder()
{
}

HRESULT WINAPI CRegFolder::Initialize(const REGFOLDERCREATEPARAMETERS *p)
{
    memcpy(&m_guid, &p->clsid, sizeof(m_guid));

    m_ItemAttributes = p->ItemAttributes;
    m_ItemAttributeCount = p->ItemAttributeCount;

    m_rootPath = p->lpszPath;
    if (!m_rootPath)
        return E_OUTOFMEMORY;

    m_enumKeyName = p->lpszEnumKeyName;
    if (!m_enumKeyName)
        return E_OUTOFMEMORY;

    m_pidlRoot.Attach(ILClone(p->pidlRoot));
    if (!m_pidlRoot)
        return E_OUTOFMEMORY;

    return S_OK;
}

PCREGFOLDERITEMATTRIBUTES CRegFolder::GetStaticItem(REFCLSID clsid)
{
    for (UINT i = 0; i < m_ItemAttributeCount; ++i)
    {
        if (IsEqualCLSID(clsid, m_ItemAttributes[i].clsid))
            return &m_ItemAttributes[i];
    }
    return 0;
}

PCREGFOLDERITEMATTRIBUTES CRegFolder::GetStaticItem(LPCITEMIDLIST pidl)
{
    GUID const *p = _ILGetGUIDPointer(pidl);
    return p ? GetStaticItem(*p) : 0;
}

HRESULT WINAPI CRegFolder::CallBack(IShellFolder *psf, HWND hOwner, IDataObject *pdo, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
        case DFM_INVOKECOMMAND:
        {
            if (wParam == DFM_CMD_PROPERTIES)
            {
                PIDLIST_ABSOLUTE pidlFolder;
                PUITEMID_CHILD *apidl;
                UINT cidl;
                HRESULT hr = SH_GetApidlFromDataObject(pdo, &pidlFolder, &apidl, &cidl);
                if (FAILED_UNEXPECTEDLY(hr))
                    return hr;

                PCREGFOLDERITEMATTRIBUTES pItem = GetStaticItem(apidl[0]);
                if (pItem && pItem->cplcmd)
                {
                    hr = SHELL32_ExecuteControlPanelItem(hOwner, pItem->cplcmd);
                }
                else if (_ILIsBitBucket(apidl[0]))
                {
                    /* FIXME: detect the drive path of bitbucket if appropiate */
                    if (!SH_ShowRecycleBinProperties(L'C'))
                        hr = E_FAIL;
                }
                else
                {
#if DBG
                    if (_ILIsDesktop(apidl[0]))
                    {
                        FAILED_UNEXPECTEDLY(hr = E_UNEXPECTED);
                    }
#endif
                    hr = S_FALSE; // Let the default implementation load the property sheet
                }
                SHFree(pidlFolder);
                _ILFreeaPidl(apidl, cidl);
                return hr;
            }
            break;
        }
    }
    return S_OK;
}

HRESULT CRegFolder::GetGuidItemAttributes(LPCITEMIDLIST pidl, LPDWORD pdwAttributes)
{
    DWORD requested = *pdwAttributes, ret = 0;

    PCREGFOLDERITEMATTRIBUTES pItem = GetStaticItem(pidl);
    if (pItem)
    {
        ret |= (requested & pItem->sfgaof);
        requested &= ~REGFOLDERITEMSFGAOMASK;
    }

    DWORD registry = requested & ~ret;
    if (registry && HCR_GetFolderAttributes(pidl, &registry))
    {
        ret |= (requested & registry);
    }
    else if (!pItem)
    {
        ret |= (requested & (SFGAO_CANLINK | SFGAO_CANRENAME));
    }

    /* In any case, links can be created */
    ret |= (requested & SFGAO_CANLINK);

    *pdwAttributes = ret;
    return S_OK;
}

HRESULT WINAPI CRegFolder::ParseDisplayName(HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName,
        ULONG *pchEaten, PIDLIST_RELATIVE *ppidl, ULONG *pdwAttributes)
{
    LPITEMIDLIST pidl;

    if (!lpszDisplayName || !ppidl)
        return E_INVALIDARG;

    *ppidl = 0;

    if (pchEaten)
        *pchEaten = 0;

    UINT cch = wcslen(lpszDisplayName);
    if (cch < 39 || lpszDisplayName[0] != L':' || lpszDisplayName[1] != L':')
        return E_FAIL;

    pidl = _ILCreateGuidFromStrW(lpszDisplayName + 2);
    if (pidl == NULL)
        return E_FAIL;

    if (cch < 41)
    {
        *ppidl = pidl;
        if (pdwAttributes && *pdwAttributes)
        {
            GetGuidItemAttributes(*ppidl, pdwAttributes);
        }
    }
    else
    {
        HRESULT hr = SHELL32_ParseNextElement(this, hwndOwner, pbc, &pidl, lpszDisplayName + 41, pchEaten, pdwAttributes);
        if (SUCCEEDED(hr))
        {
            *ppidl = pidl;
        }
        return hr;
    }

    return S_OK;
}

HRESULT WINAPI CRegFolder::EnumObjects(HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST *ppEnumIDList)
{
    IShellFolder *psf = static_cast<IShellFolder*>(this);
    return ShellObjectCreatorInit<CRegFolderEnum>(psf, m_enumKeyName, dwFlags, IID_PPV_ARG(IEnumIDList, ppEnumIDList));
}

HRESULT WINAPI CRegFolder::BindToObject(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut)
{
    CComPtr<IPersistFolder> pFolder;
    HRESULT hr;

    if (!ppvOut || !pidl || !pidl->mkid.cb)
        return E_INVALIDARG;

    *ppvOut = NULL;

    GUID *pGUID = _ILGetGUIDPointer(pidl);
    if (!pGUID)
    {
        ERR("CRegFolder::BindToObject called for non guid item!\n");
        return E_INVALIDARG;
    }

    hr = SHELL32_BindToSF(m_pidlRoot, NULL, pidl, pGUID, riid, ppvOut);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    return S_OK;
}

HRESULT WINAPI CRegFolder::BindToStorage(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CRegFolder::CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
    if (!pidl1 || !pidl2 || pidl1->mkid.cb == 0 || pidl2->mkid.cb == 0)
    {
        ERR("Got an empty pidl!\n");
        return E_INVALIDARG;
    }

    GUID const *clsid1 = _ILGetGUIDPointer (pidl1);
    GUID const *clsid2 = _ILGetGUIDPointer (pidl2);

    if (!clsid1 && !clsid2)
    {
        ERR("Got no guid pidl!\n");
        return E_INVALIDARG;
    }
    else if (clsid1 && clsid2)
    {
        if (memcmp(clsid1, clsid2, sizeof(GUID)) == 0)
            return SHELL32_CompareChildren(this, lParam, pidl1, pidl2);

        return SHELL32_CompareDetails(this, lParam, pidl1, pidl2);
    }

    /* Guid folders come first compared to everything else */
    /* And Drives come before folders in My Computer */
    if (_ILIsMyComputer(m_pidlRoot))
    {
        return MAKE_COMPARE_HRESULT(clsid1 ? 1 : -1);
    }
    else
    {
        return MAKE_COMPARE_HRESULT(clsid1 ? -1 : 1);
    }
}

HRESULT WINAPI CRegFolder::CreateViewObject(HWND hwndOwner, REFIID riid, LPVOID *ppvOut)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CRegFolder::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD *rgfInOut)
{
    if (!rgfInOut || !apidl)
        return E_INVALIDARG;

    if (cidl == 0)
    {
        FIXME("Ask m_pidlRoot\n");
        return E_INVALIDARG;
    }

    if (*rgfInOut == 0)
        *rgfInOut = ~0;

    while(cidl > 0 && *apidl)
    {
        if (_ILIsSpecialFolder(*apidl))
            GetGuidItemAttributes(*apidl, rgfInOut);
        else
            ERR("Got an unknown pidl here!\n");
        apidl++;
        cidl--;
    }

    /* make sure SFGAO_VALIDATE is cleared, some apps depend on that */
    *rgfInOut &= ~SFGAO_VALIDATE;

    return S_OK;
}

HRESULT WINAPI CRegFolder::GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
        REFIID riid, UINT * prgfInOut, LPVOID * ppvOut)
{
    LPVOID pObj = NULL;
    HRESULT hr = E_INVALIDARG;

    if (!ppvOut)
        return hr;

    *ppvOut = NULL;

    if ((IsEqualIID (riid, IID_IExtractIconA) || IsEqualIID (riid, IID_IExtractIconW)) && (cidl == 1))
    {
        PCREGFOLDERITEMATTRIBUTES pItem = GetStaticItem(apidl[0]);
        hr = CGuidItemExtractIcon_CreateInstance(pItem, apidl[0], riid, &pObj);
    }
    else if (IsEqualIID (riid, IID_IContextMenu) && (cidl >= 1))
    {
        if (!_ILGetGUIDPointer (apidl[0]))
        {
            ERR("Got non guid item!\n");
            return E_FAIL;
        }

        IShellFolder *psf = static_cast<IShellFolder*>(this);
        IContextMenuCB *pcmcb = static_cast<IContextMenuCB*>(this);
        hr = CGuidItemContextMenu_CreateInstance(m_pidlRoot, hwndOwner, cidl, apidl, psf, pcmcb, (IContextMenu**)&pObj);
    }
    else if (IsEqualIID (riid, IID_IDataObject) && (cidl >= 1))
    {
        hr = IDataObject_Constructor (hwndOwner, m_pidlRoot, apidl, cidl, TRUE, (IDataObject **)&pObj);
    }
    else
    {
        hr = E_NOINTERFACE;
    }

    *ppvOut = pObj;
    return hr;

}

HRESULT WINAPI CRegFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl, DWORD dwFlags, LPSTRRET strRet)
{
    if (!strRet || (!_ILIsSpecialFolder(pidl) && pidl != NULL))
        return E_INVALIDARG;

    if (!pidl || !pidl->mkid.cb)
    {
        if ((GET_SHGDN_RELATION(dwFlags) == SHGDN_NORMAL) && (GET_SHGDN_FOR(dwFlags) & SHGDN_FORPARSING))
        {
            LPWSTR pszPath = (LPWSTR)CoTaskMemAlloc((MAX_PATH + 1) * sizeof(WCHAR));
            if (!pszPath)
                return E_OUTOFMEMORY;

            /* parsing name like ::{...} */
            pszPath[0] = ':';
            pszPath[1] = ':';
            SHELL32_GUIDToStringW(m_guid, &pszPath[2]);

            strRet->uType = STRRET_WSTR;
            strRet->pOleStr = pszPath;

            return S_OK;
        }
        else
        {
            BOOL bRet;
            WCHAR wstrName[MAX_PATH+1];
            bRet = HCR_GetClassNameW(m_guid, wstrName, MAX_PATH);
            if (!bRet)
                return E_FAIL;

            return SHSetStrRet(strRet, wstrName);

        }
    }

    HRESULT hr;
    GUID const *clsid = _ILGetGUIDPointer (pidl);

    /* First of all check if we need to query the name from the child item */
    if (GET_SHGDN_FOR (dwFlags) == SHGDN_FORPARSING &&
        GET_SHGDN_RELATION (dwFlags) == SHGDN_NORMAL)
    {
        int bWantsForParsing = FALSE;

        /*
            * We can only get a filesystem path from a shellfolder if the
            *  value WantsFORPARSING in CLSID\\{...}\\shellfolder exists.
            *
            * Exception: The MyComputer folder doesn't have this key,
            *   but any other filesystem backed folder it needs it.
            */
        if (clsid)
        {
            bWantsForParsing = IsEqualIID (*clsid, CLSID_MyComputer) ||
                               CLSIDKeyHasRegValue(*clsid, L"ShellFolder", L"WantsForParsing");
        }

        if (bWantsForParsing)
        {
            /*
             * we need the filesystem path to the destination folder.
             * Only the folder itself can know it
             */
            return SHELL32_GetDisplayNameOfChild (this, pidl, dwFlags, strRet);
        }
    }

    /* Allocate the buffer for the result */
    LPWSTR pszPath = (LPWSTR)CoTaskMemAlloc((MAX_PATH + 1) * sizeof(WCHAR));
    if (!pszPath)
        return E_OUTOFMEMORY;

    hr = S_OK;

    if (GET_SHGDN_FOR (dwFlags) == SHGDN_FORPARSING)
    {
        wcscpy(pszPath, m_rootPath);
        PWCHAR pItemName = &pszPath[wcslen(pszPath)];

        /* parsing name like ::{...} */
        pItemName[0] = ':';
        pItemName[1] = ':';
        SHELL32_GUIDToStringW (*clsid, &pItemName[2]);
    }
    else
    {
        /* user friendly name */
        if (!HCR_GetClassNameW (*clsid, pszPath, MAX_PATH))
            hr = E_FAIL;
    }

    if (SUCCEEDED(hr))
    {
        strRet->uType = STRRET_WSTR;
        strRet->pOleStr = pszPath;
    }
    else
    {
        CoTaskMemFree(pszPath);
    }

    return hr;
}

HRESULT WINAPI CRegFolder::SetNameOf(HWND hwndOwner, PCUITEMID_CHILD pidl,    /* simple pidl */
        LPCOLESTR lpName, DWORD dwFlags, PITEMID_CHILD *pPidlOut)
{
    GUID const *clsid = _ILGetGUIDPointer (pidl);
    LPOLESTR pStr;
    HRESULT hr;
    WCHAR szName[100];

    if (!clsid)
    {
        ERR("Pidl is not reg item!\n");
        return E_FAIL;
    }

    hr = StringFromCLSID(*clsid, &pStr);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    swprintf(szName, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CLSID\\%s", pStr);

    DWORD cbData = (wcslen(lpName) + 1) * sizeof(WCHAR);
    LONG res = SHSetValueW(HKEY_CURRENT_USER, szName, NULL, RRF_RT_REG_SZ, lpName, cbData);

    CoTaskMemFree(pStr);

    if (res == ERROR_SUCCESS)
    {
        *pPidlOut = ILClone(pidl);
        return S_OK;
    }

    return E_FAIL;
}


HRESULT WINAPI CRegFolder::GetDefaultSearchGUID(GUID *pguid)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CRegFolder::EnumSearches(IEnumExtraSearch ** ppenum)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CRegFolder::GetDefaultColumn(DWORD dwRes, ULONG *pSort, ULONG *pDisplay)
{
    if (pSort)
        *pSort = 0;
    if (pDisplay)
        *pDisplay = 0;

    return S_OK;
}

HRESULT WINAPI CRegFolder::GetDefaultColumnState(UINT iColumn, DWORD *pcsFlags)
{
    if (iColumn >= REGISTRY_COL_COUNT)
        return E_INVALIDARG;
    *pcsFlags = SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT;
    return S_OK;
}

HRESULT WINAPI CRegFolder::GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CRegFolder::GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd)
{
    if (!psd)
        return E_INVALIDARG;

    GUID const *clsid = _ILGetGUIDPointer (pidl);

    if (!clsid)
    {
        ERR("Pidl is not reg item!\n");
        return E_INVALIDARG;
    }

    switch(iColumn)
    {
        case REGISTRY_COL_NAME:
            return GetDisplayNameOf(pidl, SHGDN_NORMAL | SHGDN_INFOLDER, &psd->str);
        case REGISTRY_COL_TYPE:
            return SHSetStrRet(&psd->str, IDS_SYSTEMFOLDER);
        case REGISTRY_COL_VALUE:
            HKEY hKey;
            if (!HCR_RegOpenClassIDKey(*clsid, &hKey))
                return SHSetStrRet(&psd->str, "");

            psd->str.cStr[0] = 0x00;
            psd->str.uType = STRRET_CSTR;
            RegLoadMUIStringA(hKey, "InfoTip", psd->str.cStr, MAX_PATH, NULL, 0, NULL);
            RegCloseKey(hKey);
            return S_OK;
        default:
            /* Return an empty string when we area asked for a column we don't support.
               Only  the regfolder is supposed to do this as it supports less columns compared to other folder
               and its contents are supposed to be presented alongside items that support more columns. */
            return SHSetStrRet(&psd->str, "");
    }
    return E_FAIL;
}

HRESULT WINAPI CRegFolder::MapColumnToSCID(UINT column, SHCOLUMNID *pscid)
{
    return E_NOTIMPL;
}

/* In latest windows version this is exported but it takes different arguments! */
HRESULT CRegFolder_CreateInstance(const REGFOLDERCREATEPARAMETERS *pParams, REFIID riid, void **ppv)
{
    return ShellObjectCreatorInit<CRegFolder>(pParams, riid, ppv);
}
