/*
 * Trash virtual folder support. The trashing engine is implemented in trash.c
 *
 * Copyright (C) 2006 Mikolaj Zalewski
 * Copyright (C) 2009 Andrew Hill
 * Copyright (C) 2018 Russell Johnson
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

#include <mmsystem.h>
#include <ntquery.h>

WINE_DEFAULT_DEBUG_CHANNEL(CRecycleBin);

typedef struct
{
    int column_name_id;
    const GUID *fmtId;
    DWORD pid;
    int pcsFlags;
    int fmt;
    int cxChars;
} columninfo;

static const columninfo RecycleBinColumns[] =
{
    {IDS_SHV_COLUMN_NAME,     &FMTID_Storage,   PID_STG_NAME,        SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT,  LVCFMT_LEFT,  25},
    {IDS_SHV_COLUMN_DELFROM,  &FMTID_Displaced, PID_DISPLACED_FROM,  SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT,  LVCFMT_LEFT,  35},
    {IDS_SHV_COLUMN_DELDATE,  &FMTID_Displaced, PID_DISPLACED_DATE,  SHCOLSTATE_TYPE_DATE | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT,  15},
    {IDS_SHV_COLUMN_SIZE,     &FMTID_Storage,   PID_STG_SIZE,        SHCOLSTATE_TYPE_INT | SHCOLSTATE_ONBYDEFAULT,  LVCFMT_RIGHT, 10},
    {IDS_SHV_COLUMN_TYPE,     &FMTID_Storage,   PID_STG_STORAGETYPE, SHCOLSTATE_TYPE_INT | SHCOLSTATE_ONBYDEFAULT,  LVCFMT_LEFT,  15},
    {IDS_SHV_COLUMN_MODIFIED, &FMTID_Storage,   PID_STG_WRITETIME,   SHCOLSTATE_TYPE_DATE | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT,  15},
    /* {"creation time",  &FMTID_Storage,   PID_STG_CREATETIME, SHCOLSTATE_TYPE_DATE, LVCFMT_LEFT,  20}, */
    /* {"attribs",        &FMTID_Storage,   PID_STG_ATTRIBUTES, SHCOLSTATE_TYPE_STR,  LVCFMT_LEFT,  20}, */
};

#define COLUMN_NAME    0
#define COLUMN_DELFROM 1
#define COLUMN_DATEDEL 2
#define COLUMN_SIZE    3
#define COLUMN_TYPE    4
#define COLUMN_MTIME   5
#define COLUMNS_COUNT  6

enum { IDC_PROPERTIES, IDC_DELETE, IDC_CUT, IDC_RESTORE };
static const struct
{
    LPCSTR Verb;
    WORD CmdId, StrId;
}
g_MenuItems[] =
{
    { "undelete", IDC_RESTORE, IDS_RESTORE },
    { "cut", IDC_CUT, IDS_CUT },
    { "delete", IDC_DELETE, IDS_DELETE },
    { "properties", IDC_PROPERTIES, IDS_PROPERTIES },
};

struct ITEMPROPERTIESDATA
{
    WCHAR DisplayName[MAX_PATH];
    IStream *pStreamDO;
};

static DWORD CALLBACK ItemPropertiesThread(LPVOID ThreadParameter)
{
    CComHeapPtr<ITEMPROPERTIESDATA> pData;
    pData.Attach((ITEMPROPERTIESDATA*)ThreadParameter);
    CComPtr<IDataObject> DataObj;
    HRESULT hr = CoGetInterfaceAndReleaseStream(pData->pStreamDO, IID_PPV_ARG(IDataObject, &DataObj));
    if (FAILED(hr))
        return hr;
    CComPtr<CFileDefExt> FileDefExt;
    if (SUCCEEDED(hr = ShellObjectCreator(FileDefExt)))
    {
        FileDefExt->SetRecycledName(pData->DisplayName);
        hr = FileDefExt->Initialize(NULL, DataObj, NULL);
        if (SUCCEEDED(hr))
        {
            HPROPSHEETPAGE hPages[MAX_PROPERTY_SHEET_PAGE];
            PROPSHEETHEADERW psh = { sizeof(psh), PSH_PROPTITLE };
            psh.phpage = hPages;
            psh.pszCaption = pData->DisplayName;
            if (SUCCEEDED(FileDefExt->AddPages(AddPropSheetPageCallback, (LPARAM)&psh)))
                PropertySheetW(&psh);
        }
    }
    return hr;
}

static HRESULT ShowRecycledItemPropertiesAsync(PCIDLIST_ABSOLUTE pidlFS, LPCWSTR DisplayName)
{
    CComHeapPtr<ITEMIDLIST> folder;
    HRESULT hr = SHILClone(pidlFS, &folder);
    if (SUCCEEDED(hr))
    {
        ILRemoveLastID(folder);
        LPCITEMIDLIST pidlItem = ILFindLastID(pidlFS);
        CComPtr<IDataObject> DataObj;
        if (!FAILED_UNEXPECTEDLY(hr = SHCreateFileDataObject(folder, 1, &pidlItem, NULL, &DataObj)))
        {
            hr = E_OUTOFMEMORY;
            ITEMPROPERTIESDATA *pData = (ITEMPROPERTIESDATA*)SHAlloc(sizeof(ITEMPROPERTIESDATA));
            if (pData)
            {
                lstrcpynW(pData->DisplayName, DisplayName, _countof(pData->DisplayName));
                hr = CoMarshalInterThreadInterfaceInStream(IID_IDataObject, DataObj, &pData->pStreamDO);
                if (!FAILED_UNEXPECTEDLY(hr))
                {
                    UINT flags = CTF_COINIT | CTF_PROCESS_REF | CTF_INSIST;
                    BOOL success = SHCreateThread(ItemPropertiesThread, pData, flags, NULL);
                    hr = success ? S_OK : HResultFromWin32(GetLastError());
                }
                else
                {
                    SHFree(pData);
                }
            }
        }
    }
    return hr;
}

/*
 * Recycle Bin folder
 */

BOOL WINAPI CBSearchRecycleBin(IN PVOID Context, IN HDELFILE hDeletedFile);

static PIDLRecycleStruct * _ILGetRecycleStruct(LPCITEMIDLIST pidl);

typedef struct _SEARCH_CONTEXT
{
    PIDLRecycleStruct *pFileDetails;
    HDELFILE hDeletedFile;
    BOOL bFound;
} SEARCH_CONTEXT, *PSEARCH_CONTEXT;

static DELETED_FILE_DETAILS_W* GetDeletedFileDetails(HDELFILE hDelFile, DWORD &dwSize)
{
    if (!GetDeletedFileDetailsW(hDelFile, 0, NULL, &dwSize) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        ERR("GetDeletedFileDetailsW failed\n");
        return NULL;
    }
    DELETED_FILE_DETAILS_W *pFileDetails = (DELETED_FILE_DETAILS_W *)SHAlloc(dwSize);
    if (!pFileDetails)
    {
        ERR("No memory\n");
        return pFileDetails;
    }
    if (!GetDeletedFileDetailsW(hDelFile, dwSize, pFileDetails, NULL))
    {
        ERR("GetDeletedFileDetailsW failed\n");
        SHFree(pFileDetails);
        return NULL;
    }
    return pFileDetails;
}

static inline bool IsValidRecyclerDrive(LPCWSTR Drive)
{
    return GetDriveTypeW(Drive) == DRIVE_FIXED && Drive[1] == ':';
}

static bool IsValidRecyclerPath(LPCWSTR Path)
{
    WCHAR buf[MAX_PATH];
    lstrcpynW(buf, Path, _countof(buf)); // Don't care if the entire path does not fit
    PathStripToRootW(buf);
    return IsValidRecyclerDrive(buf);
}

static INT8 GetRecyclerDrive(PIDLRecycleStruct *pData)
{
    LPCWSTR Path = pData->szName;
    return IsValidRecyclerPath(Path) ? (Path[0] | 32) - 'a' : -1;
}

static HRESULT GetDeletedEntry(PCUITEMID_CHILD pidl, IRecycleBinFile**ppRBF)
{
    *ppRBF = NULL;
    SEARCH_CONTEXT Ctx = { _ILGetRecycleStruct(pidl) };
    if (!Ctx.pFileDetails)
        return E_INVALIDARG;
    INT8 Drive = GetRecyclerDrive(Ctx.pFileDetails);
    if (Drive < 0)
        return E_INVALIDARG;
    WCHAR Recycler[] = { LOWORD(Drive), ':', '\\', '\0' };
    if (EnumerateRecycleBinW(Recycler, CBSearchRecycleBin, &Ctx) && Ctx.bFound)
    {
        *ppRBF = IRecycleBinFileFromHandle(Ctx.hDeletedFile);
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

static HRESULT CRecyclerExtractIcon_CreateInstance(
    LPCITEMIDLIST pidl, REFIID riid, LPVOID * ppvOut)
{
    PIDLRecycleStruct *pFileDetails = _ILGetRecycleStruct(pidl);
    if (pFileDetails == NULL)
        goto fallback;

    // Try to obtain the file
    SEARCH_CONTEXT Context;
    Context.pFileDetails = pFileDetails;
    Context.bFound = FALSE;

    EnumerateRecycleBinW(NULL, CBSearchRecycleBin, &Context);
    if (Context.bFound)
    {
        // This should be executed any time, if not, there are some errors in the implementation
        IRecycleBinFile* pRecycleFile = IRecycleBinFileFromHandle(Context.hDeletedFile);

        // Query the interface from the private interface
        HRESULT hr = pRecycleFile->QueryInterface(riid, ppvOut);

        // Close the file handle as we don't need it anymore
        CloseRecycleBinHandle(Context.hDeletedFile);

        return hr;
    }

fallback:
    // In case the search fails we use a default icon
    ERR("Recycler could not retrieve the icon, this shouldn't happen\n");

    CComPtr<IDefaultExtractIconInit> initIcon;
    HRESULT hr = SHCreateDefaultExtractIcon(IID_PPV_ARG(IDefaultExtractIconInit, &initIcon));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    initIcon->SetNormalIcon(swShell32Name, 0);

    return initIcon->QueryInterface(riid, ppvOut);
}

class CRecycleBinEnum :
    public CEnumIDListBase
{
    private:
    public:
        CRecycleBinEnum();
        ~CRecycleBinEnum();
        HRESULT WINAPI Initialize(DWORD dwFlags);
        static BOOL WINAPI CBEnumRecycleBin(IN PVOID Context, IN HDELFILE hDeletedFile);
        BOOL WINAPI CBEnumRecycleBin(IN HDELFILE hDeletedFile);

        BEGIN_COM_MAP(CRecycleBinEnum)
        COM_INTERFACE_ENTRY_IID(IID_IEnumIDList, IEnumIDList)
        END_COM_MAP()
};

class CRecycleBinItemContextMenu :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IContextMenu2
{
private:
    CComPtr<CRecycleBin> m_RecycleFolder;
    PITEMID_CHILD* m_apidl;
    UINT m_cidl;
public:
    CRecycleBinItemContextMenu() : m_apidl(NULL), m_cidl(0) {}
    ~CRecycleBinItemContextMenu()
    {
        _ILFreeaPidl(m_apidl, m_cidl);
    }
    static HRESULT CreateInstance(CRecycleBin*Folder, UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                                  REFIID riid, void **ppv)
    {
        return ShellObjectCreatorInit<CRecycleBinItemContextMenu>(Folder, cidl, apidl, riid, ppv);
    }

    HRESULT WINAPI Initialize(CRecycleBin*Folder, UINT cidl, PCUITEMID_CHILD_ARRAY apidl);

    // IContextMenu
    STDMETHOD(QueryContextMenu)(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override;
    STDMETHOD(InvokeCommand)(LPCMINVOKECOMMANDINFO lpcmi) override;
    STDMETHOD(GetCommandString)(UINT_PTR idCommand, UINT uFlags, UINT *lpReserved, LPSTR lpszName, UINT uMaxNameLen) override;

    // IContextMenu2
    STDMETHOD(HandleMenuMsg)(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

    BEGIN_COM_MAP(CRecycleBinItemContextMenu)
    COM_INTERFACE_ENTRY_IID(IID_IContextMenu, IContextMenu)
    COM_INTERFACE_ENTRY_IID(IID_IContextMenu2, IContextMenu2)
    END_COM_MAP()
    DECLARE_NOT_AGGREGATABLE(CRecycleBinItemContextMenu)
};

BOOL WINAPI CBSearchRecycleBin(IN PVOID Context, IN HDELFILE hDeletedFile)
{
    PSEARCH_CONTEXT pContext = (PSEARCH_CONTEXT)Context;
    DWORD dwSize;
    PDELETED_FILE_DETAILS_W pFileDetails = GetDeletedFileDetails(hDeletedFile, dwSize);
    BOOL ret;

    if (!pFileDetails)
        return FALSE;

    ret = memcmp(pFileDetails, pContext->pFileDetails, dwSize);
    if (!ret)
    {
        pContext->hDeletedFile = hDeletedFile;
        pContext->bFound = TRUE;
    }
    else
        CloseRecycleBinHandle(hDeletedFile);

    SHFree(pFileDetails);
    return ret;
}

static PIDLRecycleStruct * _ILGetRecycleStruct(LPCITEMIDLIST pidl)
{
    LPPIDLDATA pdata = _ILGetDataPointer(pidl);

    if (pdata && pdata->type == 0x00)
        return (PIDLRecycleStruct*) & (pdata->u.crecycle);

    return NULL;
}

CRecycleBinEnum::CRecycleBinEnum()
{
}

CRecycleBinEnum::~CRecycleBinEnum()
{
}

HRESULT WINAPI CRecycleBinEnum::Initialize(DWORD dwFlags)
{
    WCHAR szDrive[8];
    if (!GetEnvironmentVariableW(L"SystemDrive", szDrive, _countof(szDrive) - 1))
    {
        ERR("GetEnvironmentVariableW failed\n");
        return E_FAIL;
    }
    PathAddBackslashW(szDrive);

    if (dwFlags & SHCONTF_NONFOLDERS)
    {
        TRACE("Starting Enumeration\n");

        if (!EnumerateRecycleBinW(szDrive, CBEnumRecycleBin, this))
        {
            WARN("Error: EnumerateCRecycleBinW failed\n");
            return E_FAIL;
        }
    }
    else
    {
        // do nothing
    }
    return S_OK;
}

static LPITEMIDLIST _ILCreateRecycleItem(PDELETED_FILE_DETAILS_W pFileDetails)
{
    PIDLDATA tmp;
    LPITEMIDLIST pidl;
    PIDLRecycleStruct * p;
    int size0 = (char*)&tmp.u.crecycle.szName - (char*)&tmp.u.crecycle;
    int size = size0;

    tmp.type = 0x00;
    size += (wcslen(pFileDetails->FileName) + 1) * sizeof(WCHAR);

    pidl = (LPITEMIDLIST)SHAlloc(size + 4);
    if (!pidl)
        return pidl;

    pidl->mkid.cb = size + 2;
    memcpy(pidl->mkid.abID, &tmp, 2 + size0);

    p = &((PIDLDATA*)pidl->mkid.abID)->u.crecycle;
    RtlCopyMemory(p, pFileDetails, sizeof(DELETED_FILE_DETAILS_W));
    wcscpy(p->szName, pFileDetails->FileName);
    *(WORD*)((char*)pidl + (size + 2)) = 0;
    return pidl;
}

BOOL WINAPI CRecycleBinEnum::CBEnumRecycleBin(IN PVOID Context, IN HDELFILE hDeletedFile)
{
    return static_cast<CRecycleBinEnum *>(Context)->CBEnumRecycleBin(hDeletedFile);
}

BOOL WINAPI CRecycleBinEnum::CBEnumRecycleBin(IN HDELFILE hDeletedFile)
{
    DWORD dwSize;
    DELETED_FILE_DETAILS_W *pFileDetails = GetDeletedFileDetails(hDeletedFile, dwSize);
    LPITEMIDLIST pidl = NULL;
    if (!pFileDetails)
        return FALSE;

    pidl = _ILCreateRecycleItem(pFileDetails);
    if (!pidl)
    {
        SHFree(pFileDetails);
        return FALSE;
    }

    BOOL ret = AddToEnumList(pidl);
    if (!ret)
        SHFree(pidl);
    SHFree(pFileDetails);
    TRACE("Returning %d\n", ret);
    CloseRecycleBinHandle(hDeletedFile);
    return ret;
}

/**************************************************************************
* IContextMenu Bitbucket Item Implementation
*/

HRESULT WINAPI CRecycleBinItemContextMenu::Initialize(CRecycleBin*Folder, UINT cidl, PCUITEMID_CHILD_ARRAY apidl)
{
    m_RecycleFolder = Folder;
    if ((m_apidl = _ILCopyaPidl(apidl, cidl)) == NULL)
        return E_OUTOFMEMORY;
    m_cidl = cidl;
    return S_OK;
}

HRESULT WINAPI CRecycleBinItemContextMenu::QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    TRACE("(%p)->(hmenu=%p indexmenu=%x cmdfirst=%x cmdlast=%x flags=%x )\n", this, hMenu, indexMenu, idCmdFirst, idCmdLast, uFlags);
    UINT HighestId = 0;
    for (SIZE_T i = 0; i < _countof(g_MenuItems); ++i)
    {
        UINT CmdId = idCmdFirst + g_MenuItems[i].CmdId;
        if (CmdId > idCmdLast)
            continue;
        if (i > 0)
            _InsertMenuItemW(hMenu, indexMenu++, TRUE, 0, MFT_SEPARATOR, NULL, MFS_ENABLED);
        LPCWSTR Text = MAKEINTRESOURCEW(g_MenuItems[i].StrId);
        UINT State = MFS_ENABLED | (g_MenuItems[i].CmdId == IDC_PROPERTIES ? MFS_DEFAULT : 0);
        if (_InsertMenuItemW(hMenu, indexMenu++, TRUE, CmdId, MFT_STRING, Text, State))
            HighestId = max(HighestId, CmdId);
    }
    return HighestId ? MAKE_HRESULT(SEVERITY_SUCCESS, 0, HighestId - idCmdFirst + 1) : 0;
}

HRESULT WINAPI CRecycleBinItemContextMenu::InvokeCommand(LPCMINVOKECOMMANDINFO lpcmi)
{
    TRACE("(%p)->(invcom=%p verb=%p wnd=%p)\n", this, lpcmi, lpcmi->lpVerb, lpcmi->hwnd);
    CHAR VerbBuf[VERBKEY_CCHMAX];
    LPCSTR Verb = GetInvokeCommandVerbA(lpcmi, VerbBuf, _countof(VerbBuf));
    INT CmdId = Verb ? -1 : LOWORD(lpcmi->lpVerb);
    for (UINT i = 0; CmdId < 0 && i < _countof(g_MenuItems); ++i)
    {
        if (!lstrcmpiA(Verb, g_MenuItems[i].Verb))
            CmdId = i;
    }

    HWND hWnd = (lpcmi->fMask & CMIC_MASK_FLAG_NO_UI) ? NULL : lpcmi->hwnd;
    FILEOP_FLAGS FoFlags = (lpcmi->fMask & CMIC_MASK_FLAG_NO_UI) ? (FOF_SILENT | FOF_NOERRORUI) : 0;
    WCHAR Buffer[MAX_PATH];
    HRESULT hr = S_OK;
    switch (CmdId)
    {
        case FCIDM_SHVIEW_DELETE: // CDefView passes this directly without trying to get the correct ID!
        case IDC_RESTORE:
        case IDC_DELETE:
        {
            // TODO: SHFileOperation all files in one single operation and somehow update INFO2 at the same time
            Buffer[0] = '?';
            Buffer[1] = UNICODE_NULL;
            int AskKind = ASK_DELETE_MULTIPLE_ITEM, Restore = CmdId == IDC_RESTORE;
            if (m_cidl == 1 )
            {
                Shell_DisplayNameOf(m_RecycleFolder, m_apidl[0], SHGDN_INFOLDER, Buffer, _countof(Buffer));
                PIDLRecycleStruct *pData = _ILGetRecycleStruct(m_apidl[0]);
                BOOL Dir = pData && (pData->Attributes & FILE_ATTRIBUTE_DIRECTORY);
                AskKind = Dir ? ASK_DELETE_FOLDER : ASK_DELETE_FILE;
            }
            else
            {
                wsprintfW(Buffer, L"%d", m_cidl);
            }
            if (!Restore && !SHELL_ConfirmYesNoW(hWnd, AskKind, Buffer))
                return S_OK;

            AbortableCommand Operation;
            for (UINT i = 0; i < m_cidl && hr != HResultFromWin32(ERROR_CANCELLED); ++i)
            {
                CComPtr<IRecycleBinFile> pRBF;
                if (Operation.IsAborted())
                {
                    hr = HResultFromWin32(ERROR_CANCELLED);
                }
                else if (SUCCEEDED(hr = GetDeletedEntry(m_apidl[i], &pRBF)))
                {
                    if (Restore)
                        hr = pRBF->Restore(hWnd, FoFlags | FOF_NOCONFIRMMKDIR);
                    else
                        hr = pRBF->Delete(NULL, TRUE);
                }
            }
            m_RecycleFolder->UpdateRecycleBinIconIfEmpty();
            return hr;
        }

        case FCIDM_SHVIEW_CUT:
        case IDC_CUT:
        {
            // FIXME: Make the PIDL format look like PT_FS so we can place them on the clipboard with a CFSTR_FILENAMEMAP
            FIXME("Incompatible PIDL\n");
            SHELL_ErrorBox(hWnd, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
            return E_NOTIMPL;
        }

        case FCIDM_SHVIEW_PROPERTIES:
        case IDC_PROPERTIES:
        {
            if (m_cidl != 1) // Windows is also unable to handle multiple files
                return S_FALSE;
            FIXME("Implement the custom property sheet for deleted items\n");
            // HACK: Display the FS properties for the deleted file inside the recycle bin
            CComPtr<IRecycleBinFile> DeletedFile;
            CComHeapPtr<WCHAR> DeletedPath;
            if (SUCCEEDED(hr = GetDeletedEntry(m_apidl[0], &DeletedFile)))
                hr = DeletedFile->GetRecycledPath(&DeletedPath);
            CComHeapPtr<ITEMIDLIST> DeletedPidl;
            if (SUCCEEDED(hr))
            {
                DeletedPidl.Attach(SHSimpleIDListFromPathW(DeletedPath));
                hr = DeletedPidl ? S_OK : E_FAIL;
            }
            if (SUCCEEDED(hr))
            {
                Shell_DisplayNameOf(m_RecycleFolder, m_apidl[0], SHGDN_INFOLDER, Buffer, _countof(Buffer));
                hr = ShowRecycledItemPropertiesAsync(DeletedPidl, Buffer);
            }
            return hr;
            /* // HACK: Display the FS properties for the deleted file inside the recycle bin
            CComPtr<IRecycleBinFile> DeletedFile;
            CComHeapPtr<WCHAR> DeletedPath;
            if (SUCCEEDED(hr = GetDeletedEntry(m_apidl[0], &DeletedFile)))
                hr = DeletedFile->GetRecycledPath(&DeletedPath);
            CComHeapPtr<ITEMIDLIST> DeletedPidl;
            if (SUCCEEDED(hr))
            {
                DeletedPidl.Attach(SHSimpleIDListFromPathW(DeletedPath));
                hr = DeletedPidl ? S_OK : E_FAIL;
            }
            SHELLEXECUTEINFOW sei = { sizeof(sei), SEE_MASK_INVOKEIDLIST, hWnd, L"properties",
                                      NULL, NULL, NULL, SW_SHOWNORMAL, NULL, DeletedPidl };
            if (SUCCEEDED(hr))
                hr = ShellExecuteExW(&sei) ? S_OK : HResultFromWin32(GetLastError());
            return hr;*/
        }
    }
    return E_INVALIDARG;
}

HRESULT WINAPI CRecycleBinItemContextMenu::GetCommandString(UINT_PTR idCommand, UINT uFlags, UINT *lpReserved, LPSTR lpszName, UINT uMaxNameLen)
{
    TRACE("(%p)->(idcom=%lx flags=%x %p name=%p len=%x)\n", this, idCommand, uFlags, lpReserved, lpszName, uMaxNameLen);
    int idx = -1;
    for (UINT i = 0; i < _countof(g_MenuItems) && idx < 0; ++i)
    {
        if (g_MenuItems[i].CmdId == idCommand)
            idx = i;
    }
    switch (uFlags | GCS_UNICODE)
    {
        case (GCS_VALIDATE | GCS_UNICODE):
            return idx < 0 ? S_FALSE : S_OK;

        case (GCS_VERB | GCS_UNICODE):
            if (idx < 0)
                return E_INVALIDARG;
            else if (uFlags & GCS_UNICODE)
                return SHAnsiToUnicode(g_MenuItems[idx].Verb, (LPWSTR)lpszName, uMaxNameLen) <= uMaxNameLen ? S_OK : E_FAIL;
            else
                return StringCchCopyA(lpszName, uMaxNameLen, g_MenuItems[idx].Verb);
    }
    return E_FAIL;
}

HRESULT WINAPI CRecycleBinItemContextMenu::HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    TRACE("CRecycleBin_IContextMenu2Item_HandleMenuMsg (%p)->(msg=%x wp=%lx lp=%lx)\n", this, uMsg, wParam, lParam);
    return E_NOTIMPL;
}

CRecycleBin::CRecycleBin()
{
    pidl = NULL;
}

CRecycleBin::~CRecycleBin()
{
    SHFree(pidl);
}

static bool IsEmpty(IRecycleBin &Bin)
{
    CComPtr<IRecycleBinEnumList> Enum;
    CComPtr<IRecycleBinFile> File;
    if (SUCCEEDED(Bin.EnumObjects(&Enum)))
        return Enum->Next(1, &File, NULL) != S_OK;
    return true;
}

void CRecycleBin::UpdateRecycleBinIconIfEmpty()
{
    CComPtr<IRecycleBin> pGlobal;
    if (SUCCEEDED(GetDefaultRecycleBin(NULL, &pGlobal)) && IsEmpty(*pGlobal))
        SHUpdateRecycleBinIcon();
}

/*************************************************************************
 * RecycleBin IPersistFolder2 interface
 */

HRESULT WINAPI CRecycleBin::GetClassID(CLSID *pClassID)
{
    TRACE("(%p, %p)\n", this, pClassID);
    if (pClassID == NULL)
        return E_INVALIDARG;
    memcpy(pClassID, &CLSID_RecycleBin, sizeof(CLSID));
    return S_OK;
}

HRESULT WINAPI CRecycleBin::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    TRACE("(%p, %p)\n", this, pidl);

    SHFree((LPVOID)this->pidl);
    this->pidl = ILClone(pidl);
    if (this->pidl == NULL)
        return E_OUTOFMEMORY;
    return S_OK;
}

HRESULT WINAPI CRecycleBin::GetCurFolder(PIDLIST_ABSOLUTE *ppidl)
{
    TRACE("\n");
    *ppidl = ILClone(pidl);
    return S_OK;
}

/*************************************************************************
 * RecycleBin IShellFolder2 interface
 */

HRESULT WINAPI CRecycleBin::ParseDisplayName(HWND hwnd, LPBC pbc,
        LPOLESTR pszDisplayName, ULONG *pchEaten, PIDLIST_RELATIVE *ppidl,
        ULONG *pdwAttributes)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}


PDELETED_FILE_DETAILS_W
UnpackDetailsFromPidl(LPCITEMIDLIST pidl)
{
    return (PDELETED_FILE_DETAILS_W)&pidl->mkid.abID;
}

HRESULT WINAPI CRecycleBin::EnumObjects(HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST *ppEnumIDList)
{
    return ShellObjectCreatorInit<CRecycleBinEnum>(dwFlags, IID_PPV_ARG(IEnumIDList, ppEnumIDList));
}

HRESULT WINAPI CRecycleBin::BindToObject(PCUIDLIST_RELATIVE pidl, LPBC pbc, REFIID riid, void **ppv)
{
    FIXME("(%p, %p, %p, %s, %p) - stub\n", this, pidl, pbc, debugstr_guid(&riid), ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI CRecycleBin::BindToStorage(PCUIDLIST_RELATIVE pidl, LPBC pbc, REFIID riid, void **ppv)
{
    FIXME("(%p, %p, %p, %s, %p) - stub\n", this, pidl, pbc, debugstr_guid(&riid), ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI CRecycleBin::CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
    PIDLRecycleStruct* pData1 = _ILGetRecycleStruct(pidl1);
    PIDLRecycleStruct* pData2 = _ILGetRecycleStruct(pidl2);
    LPWSTR pName1, pName2;

    if(!pData1 || !pData2 || LOWORD(lParam) >= COLUMNS_COUNT)
        return E_INVALIDARG;

    SHORT result;
    LONGLONG diff;
    switch (LOWORD(lParam))
    {
        case 0: /* Name */
            pName1 = PathFindFileNameW(pData1->szName);
            pName2 = PathFindFileNameW(pData2->szName);
            result = wcsicmp(pName1, pName2);
            break;
        case 1: /* Orig. Location */
            result = wcsicmp(pData1->szName, pData2->szName);
            break;
        case 2: /* Date Deleted */
            result = CompareFileTime(&pData1->DeletionTime, &pData2->DeletionTime);
            break;
        case 3: /* Size */
            diff = pData1->FileSize.QuadPart - pData2->FileSize.QuadPart;
            return MAKE_COMPARE_HRESULT(diff);
        case 4: /* Type */
            pName1 = PathFindExtensionW(pData1->szName);
            pName2 = PathFindExtensionW(pData2->szName);
            result = wcsicmp(pName1, pName2);
            break;
        case 5: /* Modified */
            result = CompareFileTime(&pData1->LastModification, &pData2->LastModification);
            break;
    }
    return MAKE_COMPARE_HRESULT(result);
}

HRESULT WINAPI CRecycleBin::CreateViewObject(HWND hwndOwner, REFIID riid, void **ppv)
{
    CComPtr<IShellView> pShellView;
    HRESULT hr = E_NOINTERFACE;

    TRACE("(%p, %p, %s, %p)\n", this, hwndOwner, debugstr_guid(&riid), ppv);

    if (!ppv)
        return hr;

    *ppv = NULL;

    if (IsEqualIID (riid, IID_IDropTarget))
    {
        hr = CRecyclerDropTarget_CreateInstance(riid, ppv);
    }
    else if (IsEqualIID (riid, IID_IContextMenu) || IsEqualIID (riid, IID_IContextMenu2))
    {
        hr = this->QueryInterface(riid, ppv);
    }
    else if (IsEqualIID (riid, IID_IShellView))
    {
        SFV_CREATE sfvparams = {sizeof(SFV_CREATE), this};
        hr = SHCreateShellFolderView(&sfvparams, (IShellView**)ppv);
    }
    else
        return hr;

    TRACE ("-- (%p)->(interface=%p)\n", this, ppv);
    return hr;

}

HRESULT WINAPI CRecycleBin::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
        SFGAOF *rgfInOut)
{
    TRACE("(%p, %d, {%p, ...}, {%x})\n", this, cidl, apidl ? apidl[0] : NULL, (unsigned int)*rgfInOut);
    if (!cidl || !apidl)
    {
        *rgfInOut &= SFGAO_FOLDER | SFGAO_DROPTARGET | SFGAO_HASPROPSHEET | SFGAO_CANLINK | SFGAO_CANRENAME;
    }
    else
    {
        SFGAOF always = SFGAO_CANMOVE |SFGAO_CANDELETE | SFGAO_HASPROPSHEET;
        SFGAOF query = (*rgfInOut & (SFGAO_FOLDER | SFGAO_COMPRESSED | SFGAO_HIDDEN |
                                     SFGAO_SYSTEM | SFGAO_ENCRYPTED | SFGAO_READONLY));
        for (UINT i = 0; query && i < cidl; ++i)
        {
            PIDLRecycleStruct *pData = _ILGetRecycleStruct(apidl[i]);
            query &= (pData ? SHELL_FSAttributesToSFAttributes(pData->Attributes) : 0);
        }
        for (UINT i = 0, validate = (*rgfInOut & SFGAO_VALIDATE); validate && i < cidl; ++i)
        {
            CComPtr<IRecycleBinFile> DeletedFile;
            CComHeapPtr<WCHAR> DeletedPath;
            if (SUCCEEDED(GetDeletedEntry(apidl[i], &DeletedFile)) &&
                SUCCEEDED(DeletedFile->GetRecycledPath(&DeletedPath)) &&
                !PathFileExistsW(DeletedPath))
            {
                *rgfInOut = 0;
                return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            }
        }
        *rgfInOut &= (always | query);
    }
    return S_OK;
}

HRESULT WINAPI CRecycleBin::GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
        REFIID riid, UINT *prgfInOut, void **ppv)
{
    LPVOID pObj = NULL;
    HRESULT hr = E_INVALIDARG;

    TRACE ("(%p)->(%p,%u,apidl=%p, %p %p)\n", this,
           hwndOwner, cidl, apidl, prgfInOut, ppv);

    if (!ppv)
        return hr;

    *ppv = NULL;

    if ((IsEqualIID (riid, IID_IContextMenu) || IsEqualIID(riid, IID_IContextMenu2)) && (cidl >= 1))
    {
        hr = CRecycleBinItemContextMenu::CreateInstance(this, cidl, apidl, riid, &pObj);
    }
    else if((IsEqualIID(riid, IID_IExtractIconA) || IsEqualIID(riid, IID_IExtractIconW)) && (cidl == 1))
    {
        hr = CRecyclerExtractIcon_CreateInstance(apidl[0], riid, &pObj);
    }
    else
        hr = E_NOINTERFACE;

    if (SUCCEEDED(hr) && !pObj)
        hr = E_OUTOFMEMORY;

    *ppv = pObj;
    TRACE ("(%p)->hr=0x%08x\n", this, hr);
    return hr;
}

HRESULT WINAPI CRecycleBin::GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET *pName)
{
    PIDLRecycleStruct *pFileDetails;
    LPWSTR pFileName;

    TRACE("(%p, %p, %x, %p)\n", this, pidl, (unsigned int)uFlags, pName);

    pFileDetails = _ILGetRecycleStruct(pidl);
    if (!pFileDetails)
    {
        pName->cStr[0] = 0;
        pName->uType = STRRET_CSTR;
        return E_INVALIDARG;
    }

    pFileName = wcsrchr(pFileDetails->szName, L'\\');
    if (!pFileName)
    {
        pName->cStr[0] = 0;
        pName->uType = STRRET_CSTR;
        return E_UNEXPECTED;
    }

    pName->pOleStr = StrDupW(pFileName + 1);
    if (pName->pOleStr == NULL)
        return E_OUTOFMEMORY;

    pName->uType = STRRET_WSTR;
    return S_OK;
}

HRESULT WINAPI CRecycleBin::SetNameOf(HWND hwnd, PCUITEMID_CHILD pidl, LPCOLESTR pszName,
                                      SHGDNF uFlags, PITEMID_CHILD *ppidlOut)
{
    TRACE("\n");
    return E_FAIL; /* not supported */
}

HRESULT WINAPI CRecycleBin::GetDefaultSearchGUID(GUID *pguid)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

HRESULT WINAPI CRecycleBin::EnumSearches(IEnumExtraSearch **ppEnum)
{
    FIXME("stub\n");
    *ppEnum = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI CRecycleBin::GetDefaultColumn(DWORD dwReserved, ULONG *pSort, ULONG *pDisplay)
{
    TRACE("(%p, %x, %p, %p)\n", this, (unsigned int)dwReserved, pSort, pDisplay);
    if (pSort)
        *pSort = 0;
    if (pDisplay)
        *pDisplay = 0;
    return S_OK;
}

HRESULT WINAPI CRecycleBin::GetDefaultColumnState(UINT iColumn, SHCOLSTATEF *pcsFlags)
{
    TRACE("(%p, %d, %p)\n", this, iColumn, pcsFlags);
    if (iColumn >= COLUMNS_COUNT)
        return E_INVALIDARG;
    *pcsFlags = RecycleBinColumns[iColumn].pcsFlags;
    return S_OK;
}

HRESULT WINAPI CRecycleBin::GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT FormatDateTime(LPWSTR buffer, int size, FILETIME * ft)
{
    FILETIME lft;
    SYSTEMTIME time;
    int ret;

    FileTimeToLocalFileTime(ft, &lft);
    FileTimeToSystemTime(&lft, &time);

    ret = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, NULL, buffer, size);
    if (ret > 0 && ret < size)
    {
        /* Append space + time without seconds */
        buffer[ret-1] = ' ';
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &time, NULL, &buffer[ret], size - ret);
    }

    return (ret != 0 ? E_FAIL : S_OK);
}

HRESULT WINAPI CRecycleBin::GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, LPSHELLDETAILS pDetails)
{
    PIDLRecycleStruct * pFileDetails;
    WCHAR buffer[MAX_PATH];
    WCHAR szTypeName[100];
    LPWSTR pszBackslash;
    UINT Length;

    TRACE("(%p, %p, %d, %p)\n", this, pidl, iColumn, pDetails);
    if (iColumn >= COLUMNS_COUNT)
        return E_FAIL;
    pDetails->fmt = RecycleBinColumns[iColumn].fmt;
    pDetails->cxChar = RecycleBinColumns[iColumn].cxChars;
    if (pidl == NULL)
        return SHSetStrRet(&pDetails->str, RecycleBinColumns[iColumn].column_name_id);

    if (iColumn == COLUMN_NAME)
        return GetDisplayNameOf(pidl, SHGDN_NORMAL, &pDetails->str);

    pFileDetails = _ILGetRecycleStruct(pidl);
    switch (iColumn)
    {
        case COLUMN_DATEDEL:
            FormatDateTime(buffer, MAX_PATH, &pFileDetails->DeletionTime);
            break;
        case COLUMN_DELFROM:
            pszBackslash = wcsrchr(pFileDetails->szName, L'\\');
            Length = (pszBackslash - pFileDetails->szName);
            memcpy((LPVOID)buffer, pFileDetails->szName, Length * sizeof(WCHAR));
            buffer[Length] = UNICODE_NULL;
            if (buffer[0] && buffer[1] == L':' && !buffer[2])
            {
                buffer[2] = L'\\';
                buffer[3] = UNICODE_NULL;
            }
            break;
        case COLUMN_SIZE:
            StrFormatKBSizeW(pFileDetails->FileSize.QuadPart, buffer, MAX_PATH);
            break;
        case COLUMN_MTIME:
            FormatDateTime(buffer, MAX_PATH, &pFileDetails->LastModification);
            break;
        case COLUMN_TYPE:
            {
                SEARCH_CONTEXT Context;
                Context.pFileDetails = pFileDetails;
                Context.bFound = FALSE;
                EnumerateRecycleBinW(NULL, CBSearchRecycleBin, (PVOID)&Context);

                if (Context.bFound)
                {
                    GetDeletedFileTypeNameW(Context.hDeletedFile, buffer, _countof(buffer), NULL);

                    CloseRecycleBinHandle(Context.hDeletedFile);
                }
                /* load localized file string */
                else if (LoadStringW(shell32_hInstance, IDS_ANY_FILE, szTypeName, _countof(szTypeName)))
                {
                    StringCchPrintfW(buffer, _countof(buffer), szTypeName, PathFindExtensionW(pFileDetails->szName));
                }

                return SHSetStrRet(&pDetails->str, buffer);
            }
        default:
            return E_FAIL;
    }

    return SHSetStrRet(&pDetails->str, buffer);
}

HRESULT WINAPI CRecycleBin::MapColumnToSCID(UINT iColumn, SHCOLUMNID *pscid)
{
    TRACE("(%p, %d, %p)\n", this, iColumn, pscid);
    if (iColumn >= COLUMNS_COUNT)
        return E_INVALIDARG;
    pscid->fmtid = *RecycleBinColumns[iColumn].fmtId;
    pscid->pid = RecycleBinColumns[iColumn].pid;
    return S_OK;
}

BOOL CRecycleBin::RecycleBinIsEmpty()
{
    CComPtr<IEnumIDList> spEnumFiles;
    HRESULT hr = EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &spEnumFiles);
    if (FAILED(hr))
        return TRUE;
    CComHeapPtr<ITEMIDLIST> spPidl;
    ULONG itemcount;
    return spEnumFiles->Next(1, &spPidl, &itemcount) != S_OK;
    }

/*************************************************************************
 * RecycleBin IContextMenu interface
 */

HRESULT WINAPI CRecycleBin::QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    TRACE("QueryContextMenu %p %p %u %u %u %u\n", this, hMenu, indexMenu, idCmdFirst, idCmdLast, uFlags );
    if (!hMenu)
        return E_INVALIDARG;
    LPCWSTR Text = MAKEINTRESOURCEW(IDS_EMPTY_BITBUCKET);
    UINT State = RecycleBinIsEmpty() ? MFS_DISABLED : MFS_ENABLED;
    UINT CmdId = idCmdFirst + IDC_EMPTY;
    _InsertMenuItemW(hMenu, indexMenu++, TRUE, CmdId, MFT_STRING, Text, State);
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, CmdId - idCmdFirst + 1);
}

HRESULT WINAPI CRecycleBin::InvokeCommand(LPCMINVOKECOMMANDINFO lpcmi)
{
    TRACE("%p %p verb %p\n", this, lpcmi, lpcmi->lpVerb);
    CHAR VerbBuf[VERBKEY_CCHMAX];
    LPCSTR Verb = GetInvokeCommandVerbA(lpcmi, VerbBuf, _countof(VerbBuf));
    INT CmdId = Verb ? -1 : LOWORD(lpcmi->lpVerb);
    if (CmdId < 0 && !lstrcmpiA(Verb, "empty"))
        CmdId = IDC_EMPTY;

    HRESULT hr = E_INVALIDARG;
    LPSHELLBROWSER lpSB;
    CComPtr<IShellView> lpSV;
    if (CmdId == IDC_EMPTY)
    {
        UINT Flags = (lpcmi->fMask & CMIC_MASK_FLAG_NO_UI) ? SHERB_NOPROGRESSUI : 0;
        hr = SHEmptyRecycleBinW(lpcmi->hwnd, NULL, Flags);
        TRACE("result %x\n", hr);
        if (hr != S_OK)
            return hr;

        // This is a nasty hack. There is nothing that says lpcmi->hwnd has to be the
        // top level browser window. And the browser does not have to be inside the
        // recycle bin to invoke this command (in this case the wrong folder is refreshed).
        lpSB = (LPSHELLBROWSER)SendMessageA(lpcmi->hwnd, CWM_GETISHELLBROWSER, 0, 0);
        if (!IS_INTRESOURCE(lpSB) && SUCCEEDED(lpSB->QueryActiveShellView(&lpSV)))
            lpSV->Refresh();
    }
    return hr;
}

HRESULT WINAPI CRecycleBin::GetCommandString(UINT_PTR idCommand, UINT uFlags, UINT *lpReserved, LPSTR lpszName, UINT uMaxNameLen)
{
    TRACE("%p %lu %u %p %p %u\n", this, idCommand, uFlags, lpReserved, lpszName, uMaxNameLen);
    switch (uFlags | GCS_UNICODE)
    {
        case (GCS_VALIDATE | GCS_UNICODE):
            return idCommand != IDC_EMPTY ? S_FALSE : S_OK;

        case (GCS_VERB | GCS_UNICODE):
            if (idCommand != IDC_EMPTY)
                return E_INVALIDARG;
            else if (uFlags & GCS_UNICODE)
                return SHAnsiToUnicode("empty", (LPWSTR)lpszName, uMaxNameLen) <= uMaxNameLen ? S_OK : E_FAIL;
            else
                return StringCchCopyA(lpszName, uMaxNameLen, "empty");
    }
    return E_FAIL;
}

/*************************************************************************
 * RecycleBin IShellPropSheetExt interface
 */

HRESULT WINAPI CRecycleBin::AddPages(LPFNSVADDPROPSHEETPAGE pfnAddPage, LPARAM lParam)
{
    FIXME("%p %p %lu\n", this, pfnAddPage, lParam);

    return E_NOTIMPL;
}

HRESULT WINAPI CRecycleBin::ReplacePage(EXPPS uPageID, LPFNSVADDPROPSHEETPAGE pfnReplaceWith, LPARAM lParam)
{
    FIXME("%p %lu %p %lu\n", this, uPageID, pfnReplaceWith, lParam);

    return E_NOTIMPL;
}

/*************************************************************************
 * RecycleBin IShellExtInit interface
 */

HRESULT WINAPI CRecycleBin::Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pdtobj, HKEY hkeyProgID)
{
    TRACE("%p %p %p %p\n", this, pidlFolder, pdtobj, hkeyProgID );
    return S_OK;
}

/**
 * Tests whether a file can be trashed
 * @param wszPath Path to the file to be trash
 * @returns TRUE if the file can be trashed, FALSE otherwise
 */
BOOL
TRASH_CanTrashFile(LPCWSTR wszPath)
{
    LONG ret;
    DWORD dwNukeOnDelete, dwType, VolSerialNumber, MaxComponentLength;
    DWORD FileSystemFlags, dwSize, dwDisposition;
    HKEY hKey;
    WCHAR szBuffer[10];
    WCHAR szKey[150] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\BitBucket\\Volume\\";

    if (wszPath[1] != L':')
    {
        /* path is UNC */
        return FALSE;
    }

    // Copy and retrieve the root path from get given string
    WCHAR wszRootPathName[MAX_PATH];
    StringCbCopyW(wszRootPathName, sizeof(wszRootPathName), wszPath);
    PathStripToRootW(wszRootPathName);

    // Test to see if the drive is fixed (non removable)
    if (GetDriveTypeW(wszRootPathName) != DRIVE_FIXED)
    {
        /* no bitbucket on removable media */
        return FALSE;
    }

    if (!GetVolumeInformationW(wszRootPathName, NULL, 0, &VolSerialNumber, &MaxComponentLength, &FileSystemFlags, NULL, 0))
    {
        ERR("GetVolumeInformationW failed with %u wszRootPathName=%s\n", GetLastError(), debugstr_w(wszRootPathName));
        return FALSE;
    }

    swprintf(szBuffer, L"%04X-%04X", LOWORD(VolSerialNumber), HIWORD(VolSerialNumber));
    wcscat(szKey, szBuffer);

    if (RegCreateKeyExW(HKEY_CURRENT_USER, szKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, &dwDisposition) != ERROR_SUCCESS)
    {
        ERR("RegCreateKeyExW failed\n");
        return FALSE;
    }

    if (dwDisposition  & REG_CREATED_NEW_KEY)
    {
        /* per default move to bitbucket */
        dwNukeOnDelete = 0;
        RegSetValueExW(hKey, L"NukeOnDelete", 0, REG_DWORD, (LPBYTE)&dwNukeOnDelete, sizeof(DWORD));
        /* per default unlimited size */
        dwSize = -1;
        RegSetValueExW(hKey, L"MaxCapacity", 0, REG_DWORD, (LPBYTE)&dwSize, sizeof(DWORD));
        RegCloseKey(hKey);
        return TRUE;
    }
    else
    {
        dwSize = sizeof(dwNukeOnDelete);
        ret = RegQueryValueExW(hKey, L"NukeOnDelete", NULL, &dwType, (LPBYTE)&dwNukeOnDelete, &dwSize);
        if (ret != ERROR_SUCCESS)
        {
            if (ret ==  ERROR_FILE_NOT_FOUND)
            {
                /* restore key and enable bitbucket */
                dwNukeOnDelete = 0;
                RegSetValueExW(hKey, L"NukeOnDelete", 0, REG_DWORD, (LPBYTE)&dwNukeOnDelete, sizeof(DWORD));
            }
            RegCloseKey(hKey);
            return TRUE;
        }
        else if (dwNukeOnDelete)
        {
            /* do not delete to bitbucket */
            RegCloseKey(hKey);
            return FALSE;
        }
        /* FIXME
         * check if bitbucket is full
         */
        RegCloseKey(hKey);
        return TRUE;
    }
}

BOOL
TRASH_TrashFile(LPCWSTR wszPath)
{
    TRACE("(%s)\n", debugstr_w(wszPath));
    return DeleteFileToRecycleBin(wszPath);
}

static void TRASH_PlayEmptyRecycleBinSound()
{
    CRegKey regKey;
    CHeapPtr<WCHAR> pszValue;
    CHeapPtr<WCHAR> pszSndPath;
    DWORD dwType, dwSize;
    LONG lError;

    lError = regKey.Open(HKEY_CURRENT_USER,
                         L"AppEvents\\Schemes\\Apps\\Explorer\\EmptyRecycleBin\\.Current",
                         KEY_READ);
    if (lError != ERROR_SUCCESS)
        return;

    lError = regKey.QueryValue(NULL, &dwType, NULL, &dwSize);
    if (lError != ERROR_SUCCESS)
        return;

    if (!pszValue.AllocateBytes(dwSize))
        return;

    lError = regKey.QueryValue(NULL, &dwType, pszValue, &dwSize);
    if (lError != ERROR_SUCCESS)
        return;

    if (dwType == REG_EXPAND_SZ)
    {
        dwSize = ExpandEnvironmentStringsW(pszValue, NULL, 0);
        if (dwSize == 0)
            return;

        if (!pszSndPath.Allocate(dwSize))
            return;

        if (ExpandEnvironmentStringsW(pszValue, pszSndPath, dwSize) == 0)
            return;
    }
    else if (dwType == REG_SZ)
    {
        /* The type is REG_SZ, no need to expand */
        pszSndPath.Attach(pszValue.Detach());
    }
    else
    {
        /* Invalid type */
        return;
    }

    PlaySoundW(pszSndPath, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

/*************************************************************************
 * SHUpdateCRecycleBinIcon                                [SHELL32.@]
 *
 * Undocumented
 */
EXTERN_C HRESULT WINAPI SHUpdateRecycleBinIcon(void)
{
    FIXME("stub\n");

    // HACK! This dwItem2 should be the icon index in the system image list that has changed.
    // FIXME: Call SHMapPIDLToSystemImageListIndex
    DWORD dwItem2 = -1;

    SHChangeNotify(SHCNE_UPDATEIMAGE, SHCNF_DWORD, NULL, &dwItem2);
    return S_OK;
}

/*************************************************************************
 *              SHEmptyRecycleBinA (SHELL32.@)
 */
HRESULT WINAPI SHEmptyRecycleBinA(HWND hwnd, LPCSTR pszRootPath, DWORD dwFlags)
{
    LPWSTR szRootPathW = NULL;
    int len;
    HRESULT hr;

    TRACE("%p, %s, 0x%08x\n", hwnd, debugstr_a(pszRootPath), dwFlags);

    if (pszRootPath)
    {
        len = MultiByteToWideChar(CP_ACP, 0, pszRootPath, -1, NULL, 0);
        if (len == 0)
            return HRESULT_FROM_WIN32(GetLastError());
        szRootPathW = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
        if (!szRootPathW)
            return E_OUTOFMEMORY;
        if (MultiByteToWideChar(CP_ACP, 0, pszRootPath, -1, szRootPathW, len) == 0)
        {
            HeapFree(GetProcessHeap(), 0, szRootPathW);
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    hr = SHEmptyRecycleBinW(hwnd, szRootPathW, dwFlags);
    HeapFree(GetProcessHeap(), 0, szRootPathW);

    return hr;
}

HRESULT WINAPI SHEmptyRecycleBinW(HWND hwnd, LPCWSTR pszRootPath, DWORD dwFlags)
{
    WCHAR szBuffer[MAX_PATH];
    DWORD count = 0;
    LONG ret;
    IShellFolder *pDesktop, *pRecycleBin;
    PIDLIST_ABSOLUTE pidlRecycleBin;
    PITEMID_CHILD pidl;
    HRESULT hr = S_OK;
    LPENUMIDLIST penumFiles;
    STRRET StrRet;

    TRACE("%p, %s, 0x%08x\n", hwnd, debugstr_w(pszRootPath), dwFlags);

    if (!(dwFlags & SHERB_NOCONFIRMATION))
    {
        hr = SHGetDesktopFolder(&pDesktop);
        if (FAILED(hr))
            return hr;
        hr = SHGetFolderLocation(NULL, CSIDL_BITBUCKET, NULL, 0, &pidlRecycleBin);
        if (FAILED(hr))
        {
            pDesktop->Release();
            return hr;
        }
        hr = pDesktop->BindToObject(pidlRecycleBin, NULL, IID_PPV_ARG(IShellFolder, &pRecycleBin));
        CoTaskMemFree(pidlRecycleBin);
        pDesktop->Release();
        if (FAILED(hr))
            return hr;
        hr = pRecycleBin->EnumObjects(hwnd, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &penumFiles);
        if (FAILED(hr))
        {
            pRecycleBin->Release();
            return hr;
        }

        if (hr != S_FALSE)
        {
            while (penumFiles->Next(1, &pidl, NULL) == S_OK && count < 2)
            {
                count++;
                pRecycleBin->GetDisplayNameOf(pidl, SHGDN_NORMAL, &StrRet);
                StrRetToBuf(&StrRet, pidl, szBuffer, _countof(szBuffer));
                CoTaskMemFree(pidl);
            }
            penumFiles->Release();
        }
        pRecycleBin->Release();

        switch (count)
        {
            case 0:
                /* no files, don't need confirmation */
                break;

            case 1:
                /* we have only one item inside the bin, so show a message box with its name */
                if (ShellMessageBoxW(shell32_hInstance, hwnd, MAKEINTRESOURCEW(IDS_DELETEITEM_TEXT), MAKEINTRESOURCEW(IDS_EMPTY_BITBUCKET),
                                   MB_ICONEXCLAMATION | MB_YESNO | MB_DEFBUTTON2, szBuffer) == IDNO)
                {
                    return S_OK;
                }
                break;

            default:
                /* we have more than one item, so show a message box with the count of the items */
                StringCbPrintfW(szBuffer, sizeof(szBuffer), L"%u", count);
                if (ShellMessageBoxW(shell32_hInstance, hwnd, MAKEINTRESOURCEW(IDS_DELETEMULTIPLE_TEXT), MAKEINTRESOURCEW(IDS_EMPTY_BITBUCKET),
                                   MB_ICONEXCLAMATION | MB_YESNO | MB_DEFBUTTON2, szBuffer) == IDNO)
                {
                    return S_OK;
                }
                break;
        }
    }

    if (dwFlags & SHERB_NOPROGRESSUI)
    {
        ret = EmptyRecycleBinW(pszRootPath);
    }
    else
    {
       /* FIXME
        * show a progress dialog
        */
        ret = EmptyRecycleBinW(pszRootPath);
    }

    if (!ret)
        return HRESULT_FROM_WIN32(GetLastError());

    if (!(dwFlags & SHERB_NOSOUND))
    {
        TRASH_PlayEmptyRecycleBinSound();
    }
    if (count)
        SHUpdateRecycleBinIcon();
    return S_OK;
}

HRESULT WINAPI SHQueryRecycleBinA(LPCSTR pszRootPath, LPSHQUERYRBINFO pSHQueryRBInfo)
{
    LPWSTR szRootPathW = NULL;
    int len;
    HRESULT hr;

    TRACE("%s, %p\n", debugstr_a(pszRootPath), pSHQueryRBInfo);

    if (pszRootPath)
    {
        len = MultiByteToWideChar(CP_ACP, 0, pszRootPath, -1, NULL, 0);
        if (len == 0)
            return HRESULT_FROM_WIN32(GetLastError());
        szRootPathW = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
        if (!szRootPathW)
            return E_OUTOFMEMORY;
        if (MultiByteToWideChar(CP_ACP, 0, pszRootPath, -1, szRootPathW, len) == 0)
        {
            HeapFree(GetProcessHeap(), 0, szRootPathW);
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    hr = SHQueryRecycleBinW(szRootPathW, pSHQueryRBInfo);
    HeapFree(GetProcessHeap(), 0, szRootPathW);

    return hr;
}

HRESULT WINAPI SHQueryRecycleBinW(LPCWSTR pszRootPath, LPSHQUERYRBINFO pSHQueryRBInfo)
{
    TRACE("%s, %p\n", debugstr_w(pszRootPath), pSHQueryRBInfo);

    if (!pszRootPath || (pszRootPath[0] == 0) ||
        !pSHQueryRBInfo || (pSHQueryRBInfo->cbSize < sizeof(SHQUERYRBINFO)))
    {
        return E_INVALIDARG;
    }

    pSHQueryRBInfo->i64Size = 0;
    pSHQueryRBInfo->i64NumItems = 0;

    CComPtr<IRecycleBin> spRecycleBin;
    HRESULT hr;
    if (FAILED_UNEXPECTEDLY((hr = GetDefaultRecycleBin(pszRootPath, &spRecycleBin))))
        return hr;

    CComPtr<IRecycleBinEnumList> spEnumList;
    hr = spRecycleBin->EnumObjects(&spEnumList);
    if (!SUCCEEDED(hr))
        return hr;

    while (TRUE)
    {
        CComPtr<IRecycleBinFile> spFile;
        hr = spEnumList->Next(1, &spFile, NULL);
        if (hr == S_FALSE)
            return S_OK;

        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        ULARGE_INTEGER Size = {};
        if (FAILED_UNEXPECTEDLY((hr = spFile->GetFileSize(&Size))))
            return hr;

        pSHQueryRBInfo->i64Size += Size.QuadPart;
        pSHQueryRBInfo->i64NumItems++;
    }

    return S_OK;
}
