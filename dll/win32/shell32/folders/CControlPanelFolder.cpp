/*
 * Control panel folder
 *
 * Copyright 2003 Martin Fuchs
 * Copyright 2009 Andrew Hill
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <precomp.h>

WINE_DEFAULT_DEBUG_CHANNEL(shell);

#define IsRegItem IsControlPanelRegItem
const CLSID* IsControlPanelRegItem(PCUITEMID_CHILD pidl)
{
    if (pidl && pidl->mkid.cb >= 2 + 1 + 16)
    {
        // FIXME: PT_GUID should not be checked here, remove when CRegFolder is fixed
        if (pidl->mkid.abID[0] == PT_GUID || pidl->mkid.abID[0] == 0x71)
            return (CLSID*)(&pidl->mkid.abID[pidl->mkid.cb - (16 + 2)]); // CLSID is the last member
    }
    return NULL;
}

static HRESULT CreateLinks(HWND hWnd, PCIDLIST_ABSOLUTE pidlFolder, UINT cidl, PCUITEMID_CHILD_ARRAY apidl)
{
    FIXME("Use SHCreateLinks\n"); // FIXME: Use SHCreateLinks
    CComPtr<IDataObject> pDataObj;
    HRESULT hr = SHCreateDataObject(pidlFolder, cidl, apidl, NULL, IID_PPV_ARG(IDataObject, &pDataObj));
    if (FAILED(hr))
        return hr;
    CComPtr<IShellFolder> psf;
    CComPtr<IDropTarget> pDT;
    if (FAILED(hr = SHGetDesktopFolder(&psf)))
        return hr;
    if (FAILED(hr = psf->CreateViewObject(hWnd, IID_PPV_ARG(IDropTarget, &pDT))))
        return hr;
    return SHSimulateDrop(pDT, pDataObj, MK_CONTROL | MK_SHIFT, NULL, NULL);
}

class CItemContextMenu :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IContextMenuCB,
    public IObjectWithSite
{
    enum { CMF_USEORGCMF = 0x80000000 };
    IShellFolder *m_pRegFolder;
    IContextMenu *m_pRegMenu;
    IUnknown *m_pSite;
    HMENU m_hRegMenu;
    PITEMID_CHILD *m_apidl;
    UINT m_cidl, m_cCpl, m_OrgCMF, m_RegLast;
    bool m_Cpl; // The focused item is a .cpl and not a RegItem.
public:
    CItemContextMenu() : m_pRegFolder(NULL), m_pRegMenu(NULL), m_pSite(NULL), m_hRegMenu(NULL),
                         m_apidl(NULL), m_cidl(0), m_cCpl(0), m_OrgCMF(0)
    {
    }
    virtual ~CItemContextMenu()
    {
        IUnknown_SetSite(m_pRegMenu, NULL);
        IUnknown_Set((IUnknown**)&m_pRegMenu, NULL);
        IUnknown_Set((IUnknown**)&m_pRegFolder, NULL);
        IUnknown_Set((IUnknown**)&m_pSite, NULL);
        if (m_hRegMenu)
            DestroyMenu(m_hRegMenu);
        _ILFreeaPidl(m_apidl, m_cidl);
    }
    HRESULT WINAPI Initialize(IShellFolder *pRegFolder, UINT cidl, PCUITEMID_CHILD_ARRAY apidl)
    {
        if (cidl && ILFindLastID(apidl[0]) != apidl[0])
            return E_INVALIDARG;
        IUnknown_Set((IUnknown**)&m_pRegFolder, pRegFolder);
        m_Cpl = cidl && !IsRegItem(apidl[0]);
        m_apidl = _ILCopyaPidl(apidl, m_cidl = cidl);
        if (!m_apidl)
            return E_OUTOFMEMORY;
        // We need to reorder the items so we can hide the .cpl items from CRegFolder
        for (UINT i = 0; i < m_cidl; ++i)
        {
            if (IsRegItem(m_apidl[i]))
            {
                PITEMID_CHILD temp = m_apidl[0];
                m_apidl[0] = m_apidl[i];
                m_apidl[i] = temp;
            }
            else
            {
                m_cCpl++;
            }
        }
        if (m_cCpl < m_cidl && !m_pRegFolder)
            return E_INVALIDARG;
        return S_OK;
    }

    STDMETHOD(CallBack)(IShellFolder *psf, HWND hWnd, IDataObject *pdo, UINT uMsg, WPARAM wParam, LPARAM lParam);
    STDMETHOD(SetSite)(IUnknown*pUnkSite) override
    {
        IUnknown_Set(&m_pSite, pUnkSite);
        return S_OK;
    }
    STDMETHOD(GetSite)(REFIID riid, void**ppv) override { return m_pSite ? m_pSite->QueryInterface(riid, ppv) : E_FAIL; }

    static HRESULT Create(HWND hWnd, IShellFolder* psf, IShellFolder *pRegFolder, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, void **ppv)
    {
        CComPtr<IContextMenuCB> pcmcb;
        HRESULT hr = ShellObjectCreatorInit<CItemContextMenu>(pRegFolder, cidl, apidl, IID_PPV_ARG(IContextMenuCB, &pcmcb));
        if (FAILED(hr))
            return hr;
        DEFCONTEXTMENU dcm = { hWnd, pcmcb, NULL, psf, cidl, apidl };
        return SHCreateDefaultContextMenu(&dcm, riid, ppv);
    }

    static LPCSTR GetVerbString(UINT_PTR Id)
    {
        switch (Id)
        {
            case CControlPanelFolder::IDC_OPEN: return "open";
            case CControlPanelFolder::IDC_RUNAS: return "RunAs";
            case CControlPanelFolder::IDC_CREATELINK: return "link"; // DFM_CMD_LINK
            default: return NULL;
        }
    }

    HRESULT MapRegMenuVerbToCplCmdId(LPCSTR verb)
    {
        char buf[MAX_PATH];
        HRESULT hr = IS_INTRESOURCE(verb) ? E_FAIL : S_OK;
        if (FAILED(hr) && m_pRegMenu)
        {
            hr = GetCommandStringA(m_pRegMenu, (SIZE_T)verb, GCS_VERB, buf, _countof(buf));
            verb = buf;
        }
        UINT tmp;
        if (SUCCEEDED(hr) && !lstrcmpiA(verb, GetVerbString(tmp = CControlPanelFolder::IDC_OPEN)))
            return tmp;
        if (SUCCEEDED(hr) && !lstrcmpiA(verb, GetVerbString(tmp = CControlPanelFolder::IDC_RUNAS)))
            return tmp;
        if (SUCCEEDED(hr) && MapVerbToDfmCmd(verb) == DFM_CMD_LINK)
            return CControlPanelFolder::IDC_CREATELINK;
        return SUCCEEDED(hr) ? HRESULT_FROM_WIN32(ERROR_INVALID_NAME) : hr;
    }

    BEGIN_COM_MAP(CItemContextMenu)
    COM_INTERFACE_ENTRY_IID(IID_IContextMenuCB, IContextMenuCB)
    COM_INTERFACE_ENTRY_IID(IID_IObjectWithSite, IObjectWithSite)
    END_COM_MAP()
};

HRESULT WINAPI CItemContextMenu::CallBack(IShellFolder *psf, HWND hWnd, IDataObject *pdo, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case DFM_MODIFYQCMFLAGS:
        {
            C_ASSERT(CMF_USEORGCMF & CMF_RESERVED);
            m_OrgCMF = CMF_USEORGCMF | (UINT)wParam;
            *(UINT*)lParam = ((UINT)wParam & ~CMF_CANRENAME) | CMF_VERBSONLY | CMF_NOVERBS | CMF_DEFAULTONLY; // We want to build the menu ourself
            return S_OK;
        }
        case DFM_MERGECONTEXTMENU:
        {
            QCMINFO &qcmi = *(QCMINFO*)lParam;
            UINT cmf = (m_OrgCMF & CMF_USEORGCMF) ? (m_OrgCMF & ~CMF_USEORGCMF) : (UINT)wParam;
            UINT mfs_default = (cmf & CMF_NODEFAULT) ? 0 : MFS_DEFAULT, pos = 0, base = qcmi.idCmdFirst;
            if (m_Cpl)
            {
                InsertCMMergeMenuItem(&qcmi, &pos, base + CControlPanelFolder::IDC_OPEN, MAKEINTRESOURCEW(IDS_OPEN_VERB), MFS_ENABLED | mfs_default);
                if (!(cmf & CMF_DEFAULTONLY))
                {
                    if (cmf & CMF_EXTENDEDVERBS)
                        InsertCMMergeMenuItem(&qcmi, &pos, base + CControlPanelFolder::IDC_RUNAS, MAKEINTRESOURCEW(IDS_RUNAS_VERB), MFS_ENABLED);
                    InsertCMMergeMenuItem(&qcmi, &pos, IDC_STATIC, NULL, MFT_SEPARATOR | MFS_ENABLED);
                    InsertCMMergeMenuItem(&qcmi, &pos, base + CControlPanelFolder::IDC_CREATELINK, MAKEINTRESOURCEW(IDS_CREATELINK), MFS_ENABLED);
                }
            }
            if (m_cidl > m_cCpl)
            {
                HRESULT hr = m_pRegFolder->GetUIObjectOf(hWnd, m_cidl - m_cCpl, m_apidl, IID_NULL_PPV_ARG(IContextMenu, &m_pRegMenu));
                if (SUCCEEDED(hr))
                {
                    IUnknown_SetSite(m_pRegMenu, m_pSite); // For CDefaultContextMenu::TryToBrowse
                    UINT first = m_Cpl ? 1 : qcmi.idCmdFirst, last = m_Cpl ? 0x7fff : qcmi.idCmdLast;
                    HMENU hMenu = qcmi.hmenu;
                    if (m_Cpl)
                    {
                        hMenu = m_hRegMenu = CreatePopupMenu();
                        cmf |= CMF_NODEFAULT | CMF_OPTIMIZEFORINVOKE;
                    }
                    hr = hMenu ? m_pRegMenu->QueryContextMenu(hMenu, Pos, first, last, cmf & ~(m_cCpl ? CMF_CANRENAME : 0)) : E_FAIL;
                    m_RegLast = hr > 0 ? hr - 1 : 0;DbgDumpMenu(hMenu);
                }
                qcmi.idCmdFirst = qcmi.idCmdLast; // Ignore the HRESULT and claim the entire range instead
            }
            return S_FALSE; // Don't add more verbs (FIXME: CDefaultContextMenu does not respect this)
        }
        case DFM_INVOKECOMMAND:
        {
            CMINVOKECOMMANDINFOEX ici { sizeof(ici), CMIC_MASK_UNICODE, hWnd, (LPSTR)wParam };
            ici.lpVerbW = (LPWSTR)ici.lpVerb;
            ici.lpParametersW = (LPWSTR)lParam;
            ici.nShow = SW_SHOW;
            DFMICS dfmics = { sizeof(dfmics), ici.fMask, lParam, 0, 0, (LPCMINVOKECOMMANDINFO)&ici, m_pSite };
            return CallBack(psf, hWnd, pdo, DFM_INVOKECOMMANDEX, wParam, (LPARAM)&dfmics);
        }
        case DFM_INVOKECOMMANDEX:
        {
            if (!lParam)
                break;
            CMINVOKECOMMANDINFOEX &ici = *(CMINVOKECOMMANDINFOEX*)(((DFMICS*)lParam)->pici);

            // If the verb is "link", we want to invoke the command only once to avoid multiple questions about creating them on the desktop.
            if ((m_Cpl ? wParam : MapRegMenuVerbToCplCmdId(ici.lpVerb)) == CControlPanelFolder::IDC_CREATELINK)
            {
                CreateLinks(hWnd, static_cast<CControlPanelFolder*>(psf)->pidlRoot, m_cidl, m_apidl);
                return S_OK;
            }

            if (m_cidl > m_cCpl && m_pRegMenu)
            {
                WCHAR verbW[MAX_PATH];
                if (m_Cpl) // We must try to map the .cpl verb to a RegItem verb
                {
                    ici.lpVerb = GetVerbString(wParam);
                    ici.lpVerbW = SHAnsiToUnicode(ici.lpVerb, verbW, _countof(verbW)) ? verbW : NULL;
#if 1               // FIXME: CDefaultContextMenu::InvokeCommand should handle all string verbs
                    INT_PTR id = GetMenuIdFromVerbA(m_pRegMenu, ici.lpVerb, 0, m_RegLast);
                    if (id != -1)
                    {
                        ici.lpVerb = MAKEINTRESOURCEA(id);
                        ici.lpVerbW = MAKEINTRESOURCEW(id);
                    }
#endif
                }
                m_pRegMenu->InvokeCommand((CMINVOKECOMMANDINFO*)&ici);
            }
            UINT_PTR cplverb = wParam, mapped = 0;
            for (UINT i = m_cidl - m_cCpl; i < m_cidl; ++i)
            {
                if (!mapped++ && !m_Cpl) // We must try to map the RegItem verb to a .cpl verb
                {
                    HRESULT hr = MapRegMenuVerbToCplCmdId(ici.lpVerb);
                    if (FAILED(hr))
                        break;
                    cplverb = hr;
                }
                static_cast<CControlPanelFolder*>(psf)->InvokeCplItem(m_apidl[i], cplverb, *(CMINVOKECOMMANDINFO*)&ici);
            }
            return S_OK;
        }
    }
    return SHELL32_DefaultContextMenuCallBack(psf, pdo, uMsg);
}

/***********************************************************************
*   control panel implementation in shell namespace
*/

class CControlPanelEnum :
    public CEnumIDListBase
{
    public:
        CControlPanelEnum();
        ~CControlPanelEnum();
        HRESULT WINAPI Initialize(DWORD dwFlags, IEnumIDList* pRegEnumerator);
        BOOL RegisterCPanelApp(LPCWSTR path);
        int RegisterRegistryCPanelApps(HKEY hkey_root, LPCWSTR szRepPath);
        BOOL CreateCPanelEnumList(DWORD dwFlags);

        BEGIN_COM_MAP(CControlPanelEnum)
        COM_INTERFACE_ENTRY_IID(IID_IEnumIDList, IEnumIDList)
        END_COM_MAP()
};

/***********************************************************************
*   IShellFolder [ControlPanel] implementation
*/

static const shvheader ControlPanelSFHeader[] = {
    {IDS_SHV_COLUMN_NAME, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 20},/*FIXME*/
    {IDS_SHV_COLUMN_COMMENTS, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 80},/*FIXME*/
};

enum controlpanel_columns
{
    CONTROLPANEL_COL_NAME,
    CONTROLPANEL_COL_COMMENT,
    CONTROLPANEL_COL_COUNT,
};

CControlPanelEnum::CControlPanelEnum()
{
}

CControlPanelEnum::~CControlPanelEnum()
{
}

HRESULT WINAPI CControlPanelEnum::Initialize(DWORD dwFlags, IEnumIDList* pRegEnumerator)
{
    if (CreateCPanelEnumList(dwFlags) == FALSE)
        return E_FAIL;
    AppendItemsFromEnumerator(pRegEnumerator);
    return S_OK;
}

static LPITEMIDLIST _ILCreateCPanelApplet(LPCWSTR pszName, LPCWSTR pszDisplayName, LPCWSTR pszComment, int iIconIdx)
{
    PIDLCPanelStruct *pCP;
    LPITEMIDLIST pidl;
    LPPIDLDATA pData;
    int cchName, cchDisplayName, cchComment, cbData;

    /* Calculate lengths of given strings */
    cchName = wcslen(pszName);
    cchDisplayName = wcslen(pszDisplayName);
    cchComment = wcslen(pszComment);

    /* Allocate PIDL */
    cbData = sizeof(pidl->mkid.cb) + sizeof(pData->type) + sizeof(pData->u.cpanel) - sizeof(pData->u.cpanel.szName)
             + (cchName + cchDisplayName + cchComment + 3) * sizeof(WCHAR);
    pidl = (LPITEMIDLIST)SHAlloc(cbData + sizeof(WORD));
    if (!pidl)
        return NULL;

    /* Copy data to allocated memory */
    pidl->mkid.cb = cbData;
    pData = (PIDLDATA *)pidl->mkid.abID;
    pData->type = PT_CPLAPPLET;

    pCP = &pData->u.cpanel;
    pCP->dummy = 0;
    pCP->iconIdx = iIconIdx;
    wcscpy(pCP->szName, pszName);
    pCP->offsDispName = cchName + 1;
    wcscpy(pCP->szName + pCP->offsDispName, pszDisplayName);
    pCP->offsComment = pCP->offsDispName + cchDisplayName + 1;
    wcscpy(pCP->szName + pCP->offsComment, pszComment);

    /* Add PIDL NULL terminator */
    *(WORD*)(pCP->szName + pCP->offsComment + cchComment + 1) = 0;

    pcheck(pidl);

    return pidl;
}

/**************************************************************************
 *  _ILGetCPanelPointer()
 * gets a pointer to the control panel struct stored in the pidl
 */
static PIDLCPanelStruct *_ILGetCPanelPointer(LPCITEMIDLIST pidl)
{
    LPPIDLDATA pdata = _ILGetDataPointer(pidl);

    if (pdata && pdata->type == PT_CPLAPPLET)
        return (PIDLCPanelStruct *) & (pdata->u.cpanel);

    return NULL;
}

HRESULT CCPLExtractIcon_CreateInstance(IShellFolder * psf, LPCITEMIDLIST pidl, REFIID riid, LPVOID * ppvOut)
{
    PIDLCPanelStruct *pData = _ILGetCPanelPointer(pidl);
    if (!pData)
        return E_FAIL;

    CComPtr<IDefaultExtractIconInit> initIcon;
    HRESULT hr = SHCreateDefaultExtractIcon(IID_PPV_ARG(IDefaultExtractIconInit, &initIcon));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    initIcon->SetNormalIcon(pData->szName, (int)pData->iconIdx != -1 ? pData->iconIdx : 0);

    return initIcon->QueryInterface(riid, ppvOut);
}

BOOL CControlPanelEnum::RegisterCPanelApp(LPCWSTR wpath)
{
    CPlApplet* applet = Control_LoadApplet(0, wpath, NULL);
    int iconIdx;

    if (applet)
    {
        for (UINT i = 0; i < applet->count; ++i)
        {
            if (applet->info[i].idIcon > 0)
                iconIdx = -applet->info[i].idIcon; /* negative icon index instead of icon number */
            else
                iconIdx = 0;

            LPITEMIDLIST pidl = _ILCreateCPanelApplet(wpath,
                                                      applet->info[i].name,
                                                      applet->info[i].info,
                                                      iconIdx);

            if (pidl)
                AddToEnumList(pidl);
        }
        Control_UnloadApplet(applet);
    }
    return TRUE;
}

int CControlPanelEnum::RegisterRegistryCPanelApps(HKEY hkey_root, LPCWSTR szRepPath)
{
    WCHAR name[MAX_PATH];
    WCHAR value[MAX_PATH];
    HKEY hkey;

    int cnt = 0;

    if (RegOpenKeyW(hkey_root, szRepPath, &hkey) == ERROR_SUCCESS)
    {
        int idx = 0;

        for(; ; idx++)
        {
            DWORD nameLen = MAX_PATH;
            DWORD valueLen = MAX_PATH;
            WCHAR buffer[MAX_PATH];

            if (RegEnumValueW(hkey, idx, name, &nameLen, NULL, NULL, (LPBYTE)&value, &valueLen) != ERROR_SUCCESS)
                break;

            if (ExpandEnvironmentStringsW(value, buffer, MAX_PATH))
            {
                wcscpy(value, buffer);
            }

            if (RegisterCPanelApp(value))
                ++cnt;
        }
        RegCloseKey(hkey);
    }

    return cnt;
}

/**************************************************************************
 *  CControlPanelEnum::CreateCPanelEnumList()
 */
BOOL CControlPanelEnum::CreateCPanelEnumList(DWORD dwFlags)
{
    WCHAR szPath[MAX_PATH];
    WIN32_FIND_DATAW wfd;
    HANDLE hFile;

    TRACE("(%p)->(flags=0x%08x)\n", this, dwFlags);

    /* enumerate the control panel applets */
    if (dwFlags & SHCONTF_NONFOLDERS)
    {
        LPWSTR p;

        GetSystemDirectoryW(szPath, MAX_PATH);
        p = PathAddBackslashW(szPath);
        wcscpy(p, L"*.cpl");

        hFile = FindFirstFileW(szPath, &wfd);

        if (hFile != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(dwFlags & SHCONTF_INCLUDEHIDDEN) && (wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
                    continue;

                if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    wcscpy(p, wfd.cFileName);
                    if (wcscmp(wfd.cFileName, L"ncpa.cpl"))
                        RegisterCPanelApp(szPath);
                }
            } while(FindNextFileW(hFile, &wfd));
            FindClose(hFile);
        }

        RegisterRegistryCPanelApps(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Control Panel\\Cpls");
        RegisterRegistryCPanelApps(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Control Panel\\Cpls");
    }
    return TRUE;
}

CControlPanelFolder::CControlPanelFolder()
{
    pidlRoot = NULL;    /* absolute pidl */
}

CControlPanelFolder::~CControlPanelFolder()
{
    TRACE("-- destroying IShellFolder(%p)\n", this);
    SHFree(pidlRoot);
}

/**************************************************************************
*    CControlPanelFolder::ParseDisplayName
*/
HRESULT WINAPI CControlPanelFolder::ParseDisplayName(
    HWND hwndOwner,
    LPBC pbc,
    LPOLESTR lpszDisplayName,
    DWORD *pchEaten,
    PIDLIST_RELATIVE *ppidl,
    DWORD *pdwAttributes)
{
    /* We only support parsing guid names */
    return m_regFolder->ParseDisplayName(hwndOwner, pbc, lpszDisplayName, pchEaten, ppidl, pdwAttributes);
}

/**************************************************************************
*        CControlPanelFolder::EnumObjects
*/
HRESULT WINAPI CControlPanelFolder::EnumObjects(HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST *ppEnumIDList)
{
    CComPtr<IEnumIDList> pRegEnumerator;
    m_regFolder->EnumObjects(hwndOwner, dwFlags, &pRegEnumerator);

    return ShellObjectCreatorInit<CControlPanelEnum>(dwFlags, pRegEnumerator, IID_PPV_ARG(IEnumIDList, ppEnumIDList));
}

/**************************************************************************
*        CControlPanelFolder::BindToObject
*/
HRESULT WINAPI CControlPanelFolder::BindToObject(
    PCUIDLIST_RELATIVE pidl,
    LPBC pbcReserved,
    REFIID riid,
    LPVOID *ppvOut)
{
    return m_regFolder->BindToObject(pidl, pbcReserved, riid, ppvOut);
}

/**************************************************************************
*    CControlPanelFolder::BindToStorage
*/
HRESULT WINAPI CControlPanelFolder::BindToStorage(
    PCUIDLIST_RELATIVE pidl,
    LPBC pbcReserved,
    REFIID riid,
    LPVOID *ppvOut)
{
    FIXME("(%p)->(pidl=%p,%p,%s,%p) stub\n", this, pidl, pbcReserved, shdebugstr_guid(&riid), ppvOut);

    *ppvOut = NULL;
    return E_NOTIMPL;
}

/**************************************************************************
*     CControlPanelFolder::CompareIDs
*/
HRESULT WINAPI CControlPanelFolder::CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
    /* Dont use SHELL32_CompareGuidItems because it would cause guid items to come first */
    if (_ILIsSpecialFolder(pidl1) || _ILIsSpecialFolder(pidl2))
    {
        return SHELL32_CompareDetails(this, lParam, pidl1, pidl2);
    }
    PIDLCPanelStruct *pData1 = _ILGetCPanelPointer(pidl1);
    PIDLCPanelStruct *pData2 = _ILGetCPanelPointer(pidl2);

    if (!pData1 || !pData2 || LOWORD(lParam) >= CONTROLPANEL_COL_COUNT)
        return E_INVALIDARG;

    int result;
    switch(LOWORD(lParam))
    {
        case CONTROLPANEL_COL_NAME:
            result = wcsicmp(pData1->szName + pData1->offsDispName, pData2->szName + pData2->offsDispName);
            break;
        case CONTROLPANEL_COL_COMMENT:
            result = wcsicmp(pData1->szName + pData1->offsComment, pData2->szName + pData2->offsComment);
            break;
        default:
            ERR("Got wrong lParam!\n");
            return E_INVALIDARG;
    }

    return MAKE_COMPARE_HRESULT(result);
}

/**************************************************************************
*    CControlPanelFolder::CreateViewObject
*/
HRESULT WINAPI CControlPanelFolder::CreateViewObject(HWND hwndOwner, REFIID riid, LPVOID * ppvOut)
{
    CComPtr<IShellView>                    pShellView;
    HRESULT hr = E_INVALIDARG;

    TRACE("(%p)->(hwnd=%p,%s,%p)\n", this, hwndOwner, shdebugstr_guid(&riid), ppvOut);

    if (ppvOut) {
        *ppvOut = NULL;

        if (IsEqualIID(riid, IID_IDropTarget)) {
            WARN("IDropTarget not implemented\n");
            hr = E_NOTIMPL;
        } else if (IsEqualIID(riid, IID_IContextMenu)) {
            WARN("IContextMenu not implemented\n");
            hr = E_NOTIMPL;
        } else if (IsEqualIID(riid, IID_IShellView)) {
            SFV_CREATE sfvparams = {sizeof(SFV_CREATE), this};
            hr = SHCreateShellFolderView(&sfvparams, (IShellView**)ppvOut);
        }
    }
    TRACE("--(%p)->(interface=%p)\n", this, ppvOut);
    return hr;
}

/**************************************************************************
*  CControlPanelFolder::GetAttributesOf
*/
HRESULT WINAPI CControlPanelFolder::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD * rgfInOut)
{
    HRESULT hr = S_OK;
    static const DWORD dwControlPanelAttributes =
        SFGAO_HASSUBFOLDER | SFGAO_FOLDER | SFGAO_CANLINK;

    TRACE("(%p)->(cidl=%d apidl=%p mask=%p (0x%08x))\n",
          this, cidl, apidl, rgfInOut, rgfInOut ? *rgfInOut : 0);

    if (!rgfInOut)
        return E_INVALIDARG;
    if (cidl && !apidl)
        return E_INVALIDARG;

    if (*rgfInOut == 0)
        *rgfInOut = ~0;

    if (!cidl)
    {
        *rgfInOut &= dwControlPanelAttributes;
    }
    else
    {
        while(cidl > 0 && *apidl)
        {
            pdump(*apidl);
            if (_ILIsCPanelStruct(*apidl))
                *rgfInOut &= SFGAO_CANLINK;
            else if (_ILIsSpecialFolder(*apidl))
                m_regFolder->GetAttributesOf(1, apidl, rgfInOut);
            else
                ERR("Got unknown pidl\n");
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
*    CControlPanelFolder::GetUIObjectOf
*
* PARAMETERS
*  HWND           hwndOwner, //[in ] Parent window for any output
*  UINT           cidl,      //[in ] array size
*  LPCITEMIDLIST* apidl,     //[in ] simple pidl array
*  REFIID         riid,      //[in ] Requested Interface
*  UINT*          prgfInOut, //[   ] reserved
*  LPVOID*        ppvObject) //[out] Resulting Interface
*
*/
HRESULT WINAPI CControlPanelFolder::GetUIObjectOf(HWND hwndOwner,
        UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT * prgfInOut, LPVOID * ppvOut)
{
    LPVOID pObj = NULL;
    HRESULT hr = E_INVALIDARG;

    TRACE("(%p)->(%p,%u,apidl=%p,%s,%p,%p)\n",
          this, hwndOwner, cidl, apidl, shdebugstr_guid(&riid), prgfInOut, ppvOut);

    if (ppvOut) {
        *ppvOut = NULL;

        if (IsEqualIID(riid, IID_IContextMenu) && (cidl >= 1)) {
            hr = CItemContextMenu::Create(hwndOwner, static_cast<IShellFolder*>(this), m_regFolder, cidl, apidl, riid, &pObj);
        } else if (IsEqualIID(riid, IID_IDataObject) && (cidl >= 1)) {
            hr = IDataObject_Constructor(hwndOwner, pidlRoot, apidl, cidl, TRUE, (IDataObject **)&pObj);
        } else if ((IsEqualIID(riid, IID_IExtractIconA) || IsEqualIID(riid, IID_IExtractIconW)) && (cidl == 1)) {
            if (_ILGetCPanelPointer(apidl[0]))
                hr = CCPLExtractIcon_CreateInstance(this, apidl[0], riid, &pObj);
            else
                hr = m_regFolder->GetUIObjectOf(hwndOwner, cidl, apidl, riid, prgfInOut, &pObj);
        } else {
            hr = E_NOINTERFACE;
        }

        if (SUCCEEDED(hr) && !pObj)
            hr = E_OUTOFMEMORY;

        *ppvOut = pObj;
    }
    TRACE("(%p)->hr=0x%08x\n", this, hr);
    return hr;
}

/**************************************************************************
*    CControlPanelFolder::GetDisplayNameOf
*/
HRESULT WINAPI CControlPanelFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl, DWORD dwFlags, LPSTRRET strRet)
{
    if (!pidl)
        return S_FALSE;

    PIDLCPanelStruct *pCPanel = _ILGetCPanelPointer(pidl);

    if (pCPanel)
    {
        return SHSetStrRet(strRet, pCPanel->szName + pCPanel->offsDispName);
    }
    else if (_ILIsSpecialFolder(pidl))
    {
        return m_regFolder->GetDisplayNameOf(pidl, dwFlags, strRet);
    }

    return E_FAIL;
}

/**************************************************************************
*  CControlPanelFolder::SetNameOf
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
HRESULT WINAPI CControlPanelFolder::SetNameOf(HWND hwndOwner, PCUITEMID_CHILD pidl,    /*simple pidl */
        LPCOLESTR lpName, DWORD dwFlags, PITEMID_CHILD *pPidlOut)
{
    FIXME("(%p)->(%p,pidl=%p,%s,%u,%p)\n", this, hwndOwner, pidl, debugstr_w(lpName), dwFlags, pPidlOut);
    return E_FAIL;
}

HRESULT WINAPI CControlPanelFolder::GetDefaultSearchGUID(GUID *pguid)
{
    FIXME("(%p)\n", this);
    return E_NOTIMPL;
}

HRESULT WINAPI CControlPanelFolder::EnumSearches(IEnumExtraSearch **ppenum)
{
    FIXME("(%p)\n", this);
    return E_NOTIMPL;
}

HRESULT WINAPI CControlPanelFolder::GetDefaultColumn(DWORD dwRes, ULONG *pSort, ULONG *pDisplay)
{
    TRACE("(%p)\n", this);

    if (pSort) *pSort = 0;
    if (pDisplay) *pDisplay = 0;
    return S_OK;
}

HRESULT WINAPI CControlPanelFolder::GetDefaultColumnState(UINT iColumn, DWORD *pcsFlags)
{
    TRACE("(%p)\n", this);

    if (!pcsFlags || iColumn >= CONTROLPANEL_COL_COUNT)
        return E_INVALIDARG;
    *pcsFlags = ControlPanelSFHeader[iColumn].colstate;
    return S_OK;
}

HRESULT WINAPI CControlPanelFolder::GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv)
{
    FIXME("(%p)\n", this);
    return E_NOTIMPL;
}

HRESULT WINAPI CControlPanelFolder::GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd)
{
    if (!psd || iColumn >= CONTROLPANEL_COL_COUNT)
        return E_INVALIDARG;

    if (!pidl)
    {
        psd->fmt = ControlPanelSFHeader[iColumn].fmt;
        psd->cxChar = ControlPanelSFHeader[iColumn].cxChar;
        return SHSetStrRet(&psd->str, shell32_hInstance, ControlPanelSFHeader[iColumn].colnameid);
    }
    else if (_ILIsSpecialFolder(pidl))
    {
        return m_regFolder->GetDetailsOf(pidl, iColumn, psd);
    }
    else
    {
        PIDLCPanelStruct *pCPanel = _ILGetCPanelPointer(pidl);

        if (!pCPanel)
            return E_FAIL;

        switch(iColumn)
        {
            case CONTROLPANEL_COL_NAME:
                return SHSetStrRet(&psd->str, pCPanel->szName + pCPanel->offsDispName);
            case CONTROLPANEL_COL_COMMENT:
                return SHSetStrRet(&psd->str, pCPanel->szName + pCPanel->offsComment);
        }
    }

    return E_FAIL;
}

HRESULT WINAPI CControlPanelFolder::MapColumnToSCID(UINT column, SHCOLUMNID *pscid)
{
    FIXME("(%p)\n", this);
    return E_NOTIMPL;
}

/************************************************************************
 *    CControlPanelFolder::GetClassID
 */
HRESULT WINAPI CControlPanelFolder::GetClassID(CLSID *lpClassId)
{
    TRACE("(%p)\n", this);

    if (!lpClassId)
        return E_POINTER;
    *lpClassId = CLSID_ControlPanel;

    return S_OK;
}

/************************************************************************
 *    CControlPanelFolder::Initialize
 *
 * NOTES: it makes no sense to change the pidl
 */
HRESULT WINAPI CControlPanelFolder::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    if (pidlRoot)
        SHFree((LPVOID)pidlRoot);

    pidlRoot = ILClone(pidl);

    /* Create the inner reg folder */
    HRESULT hr;
    hr = CRegFolder_CreateInstance(&CLSID_ControlPanel,
                                   pidlRoot,
                                   L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\::{21EC2020-3AEA-1069-A2DD-08002B30309D}",
                                   L"ControlPanel",
                                   IID_PPV_ARG(IShellFolder2, &m_regFolder));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    return S_OK;
}

/**************************************************************************
 *    CControlPanelFolder::GetCurFolder
 */
HRESULT WINAPI CControlPanelFolder::GetCurFolder(PIDLIST_ABSOLUTE * pidl)
{
    TRACE("(%p)->(%p)\n", this, pidl);

    if (!pidl)
        return E_POINTER;
    *pidl = ILClone(pidlRoot);
    return S_OK;
}

HRESULT CControlPanelFolder::InvokeCplItem(PCUITEMID_CHILD pidl, UINT_PTR CmdId, CMINVOKECOMMANDINFO &ICI)
{
    HRESULT hr = E_INVALIDARG;
    HWND hWnd = (ICI.fMask & CMIC_MASK_FLAG_NO_UI) ? NULL : ICI.hwnd;
    if (PIDLCPanelStruct *pCPS = _ILGetCPanelPointer(pidl))
    {
        if (CmdId == IDC_OPEN || CmdId == IDC_RUNAS)
        {
            WCHAR params[MAX_PATH + MAX_PATH];
            wsprintfW(params, L"%s,%s", pCPS->szName, pCPS->szName + pCPS->offsDispName);
            hr = SHELL32_RunControlPanel(params, hWnd, CmdId == IDC_RUNAS);
        }
        else if (CmdId == IDC_CREATELINK)
        {
            hr = CreateLinks(hWnd, pidlRoot, 1, &pidl);
        }
    }
    return hr;
}

/**************************************************************************
* COpenControlPanel
*/

static HRESULT GetParsingName(PCIDLIST_ABSOLUTE pidl, PWSTR*Name)
{
    PIDLIST_ABSOLUTE pidlFree = NULL;
    if (IS_INTRESOURCE(pidl))
    {
        HRESULT hr = SHGetSpecialFolderLocation(NULL, (UINT)(SIZE_T)pidl, &pidlFree);
        if (FAILED(hr))
            return hr;
        pidl = pidlFree;
    }
    HRESULT hr = SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, Name);
    ILFree(pidlFree);
    return hr;
}

static HRESULT CreateCplAbsoluteParsingPath(LPCWSTR Prefix, LPCWSTR InFolderParse, PWSTR Buf, UINT cchBuf)
{
    PWSTR cpfolder;
    HRESULT hr = GetParsingName((PCIDLIST_ABSOLUTE)CSIDL_CONTROLS, &cpfolder);
    if (SUCCEEDED(hr))
    {
        hr = StringCchPrintfW(Buf, cchBuf, L"%s\\%s%s", cpfolder, Prefix, InFolderParse);
        SHFree(cpfolder);
    }
    return hr;
}

static HRESULT FindExeCplClass(LPCWSTR Canonical, HKEY hKey, BOOL Wow64, LPWSTR clsid)
{
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    HKEY hNSKey;
    WCHAR key[MAX_PATH], buf[MAX_PATH];
    wsprintfW(key, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\%s\\NameSpace",
              Wow64 ? L"ControlPanelWOW64" : L"ControlPanel");
    LSTATUS error = RegOpenKeyExW(hKey, key, 0, KEY_READ, &hNSKey);
    if (error)
        return HRESULT_FROM_WIN32(error);
    for (DWORD i = 0; RegEnumKeyW(hNSKey, i, key, _countof(key)) == ERROR_SUCCESS; ++i)
    {
        IID validate;
        if (SUCCEEDED(IIDFromString(key, &validate)))
        {
            wsprintfW(buf, L"CLSID\\%s", key);
            DWORD cb = sizeof(buf);
            if (RegGetValueW(HKEY_CLASSES_ROOT, buf, L"System.ApplicationName",
                             RRF_RT_REG_SZ, NULL, buf, &cb) == ERROR_SUCCESS)
            {
                if (!lstrcmpiW(buf, Canonical))
                {
                    lstrcpyW(clsid, key);
                    hr = S_OK;
                }
            }
        }
    }
    RegCloseKey(hNSKey);
    return hr;
}

static HRESULT FindExeCplClass(LPCWSTR Canonical, LPWSTR clsid)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr))
        hr = FindExeCplClass(Canonical, HKEY_CURRENT_USER, FALSE, clsid);
    if (FAILED(hr))
        hr = FindExeCplClass(Canonical, HKEY_CURRENT_USER, TRUE, clsid);
    if (FAILED(hr))
        hr = FindExeCplClass(Canonical, HKEY_LOCAL_MACHINE, FALSE, clsid);
    if (FAILED(hr))
        hr = FindExeCplClass(Canonical, HKEY_LOCAL_MACHINE, TRUE, clsid);
    return hr;
}

HRESULT WINAPI COpenControlPanel::Open(LPCWSTR pszName, LPCWSTR pszPage, IUnknown *punkSite)
{
    WCHAR path[MAX_PATH], clspath[MAX_PATH];
    HRESULT hr = S_OK;
    SHELLEXECUTEINFOW sei = { sizeof(sei), SEE_MASK_FLAG_DDEWAIT };
    sei.lpFile = path;
    sei.nShow = SW_SHOW;
    if (!pszName)
    {
        GetSystemDirectoryW(path, _countof(path));
        PathAppendW(path, L"control.exe");
    }
    else
    {
        LPWSTR clsid = clspath + wsprintfW(clspath, L"CLSID\\");
        if (SUCCEEDED(hr = FindExeCplClass(pszName, clsid)))
        {
            if (SUCCEEDED(hr = CreateCplAbsoluteParsingPath(L"::", clsid, path, _countof(path))))
            {
                // NT6 will execute "::{26EE0668-A00A-44D7-9371-BEB064C98683}\0\::{clsid}[\pszPage]"
                // but we don't support parsing that so we force the class instead.
                sei.fMask |= SEE_MASK_CLASSNAME;
                sei.lpClass = clspath;
            }
        }
    }

    if (SUCCEEDED(hr))
    {
        DWORD error = ShellExecuteExW(&sei) ? ERROR_SUCCESS : GetLastError();
        hr = HRESULT_FROM_WIN32(error);
    }
    return hr;
}

HRESULT WINAPI COpenControlPanel::GetPath(LPCWSTR pszName, LPWSTR pszPath, UINT cchPath)
{
    HRESULT hr;
    if (!pszName)
    {
        PWSTR cpfolder;
        if (SUCCEEDED(hr = GetParsingName((PCIDLIST_ABSOLUTE)CSIDL_CONTROLS, &cpfolder)))
        {
            hr = StringCchCopyW(pszPath, cchPath, cpfolder);
            SHFree(cpfolder);
        }
    }
    else
    {
        WCHAR clsid[38 + 1];
        if (SUCCEEDED(hr = FindExeCplClass(pszName, clsid)))
        {
            hr = CreateCplAbsoluteParsingPath(L"::", clsid, pszPath, cchPath);
        }
    }
    return hr;
}

HRESULT WINAPI COpenControlPanel::GetCurrentView(CPVIEW *pView)
{
    *pView = CPVIEW_CLASSIC;
    return S_OK;
}
