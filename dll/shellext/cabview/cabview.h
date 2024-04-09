/*
 * PROJECT:     ReactOS CabView Shell Extension
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * COPYRIGHT:   Whindmar Saksit (whindsaks@proton.me)
 */

#pragma once
#include "precomp.h"
#define FLATFOLDER TRUE

EXTERN_C const GUID CLSID_CabFolder;
extern HINSTANCE g_hInst;

inline bool IsEqual(const SHCOLUMNID &a, REFGUID bg, UINT bi)
{
    return a.pid == bi && IsEqualGUID(a.fmtid, bg);
}

#include <pshpack1.h>
struct CABITEM
{
    WORD cb;
    WORD Unknown; // Not sure what Windows uses this for, we always store 0
    UINT Size;
    WORD Date, Time; // DOS
    WORD Attrib;
    WORD NameOffset;
    WCHAR Path[ANYSIZE_ARRAY];

    bool IsFolder() const { return false; }
    enum { FSATTS = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE };
    WORD GetFSAttributes() const { return Attrib & FSATTS; }
    LPCWSTR GetName() const { return Path + NameOffset; }

    template<class PIDL> static CABITEM* Validate(PIDL pidl)
    {
        CABITEM *p = (CABITEM*)pidl;
        return p && p->cb >= FIELD_OFFSET(CABITEM, Path[1]) && p->Unknown == 0 ? p : NULL;
    }
};
#include <poppack.h>

class CEnumIDList :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IEnumIDList
{
protected:
    HDPA m_Items;
    ULONG m_Pos;

public:
    static int CALLBACK DPADestroyCallback(void *p, void *pData)
    {
        SHFree(p);
        return TRUE;
    }

    CEnumIDList() : m_Pos(0)
    {
        m_Items = DPA_Create(0);
    }

    ~CEnumIDList()
    {
        DPA_DestroyCallback(m_Items, DPADestroyCallback, NULL);
    }

    UINT GetCount() const { return m_Items ? DPA_GetPtrCount(m_Items) : 0; }
    int FindNamedItem(PCUITEMID_CHILD pidl) const;
    HRESULT Fill(LPCWSTR path, HWND hwnd = NULL, SHCONTF contf = 0);
    HRESULT Fill(PCIDLIST_ABSOLUTE pidl, HWND hwnd = NULL, SHCONTF contf = 0);

    HRESULT Append(LPCITEMIDLIST pidl)
    {
        return DPA_AppendPtr(m_Items, (void*)pidl) != DPA_ERR ? S_OK : E_OUTOFMEMORY;
    }

    // IEnumIDList
    IFACEMETHODIMP Next(ULONG celt, PITEMID_CHILD *rgelt, ULONG *pceltFetched)
    {
        if (!rgelt)
            return E_INVALIDARG;
        HRESULT hr = S_FALSE;
        UINT count = GetCount(), fetched = 0;
        if (m_Pos < count && fetched < celt)
        {
            if (SUCCEEDED(hr = SHILClone(DPA_FastGetPtr(m_Items, m_Pos), &rgelt[fetched])))
                fetched++;
        }
        if (pceltFetched)
            *pceltFetched = fetched;
        m_Pos += fetched;
        return FAILED(hr) ? hr : celt == fetched && fetched ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Reset()
    {
        m_Pos = 0;
        return S_OK;
    }

    IFACEMETHODIMP Skip(ULONG celt)
    {
        UINT count = GetCount(), newpos = m_Pos + celt;
        if (celt > count || newpos >= count)
            return E_INVALIDARG;
        m_Pos = newpos;
        return S_OK;
    }

    IFACEMETHODIMP Clone(IEnumIDList **ppenum)
    {
        UNIMPLEMENTED;
        *ppenum = NULL;
        return E_NOTIMPL;
    }

    static CEnumIDList* CreateInstance()
    {
        CComObject<CEnumIDList>* obj;
        if (FAILED(obj->CreateInstance(&obj)))
            return NULL;
        obj->AddRef();
        return obj;
    }

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CEnumIDList)

    BEGIN_COM_MAP(CEnumIDList)
        COM_INTERFACE_ENTRY_IID(IID_IEnumIDList, IEnumIDList)
    END_COM_MAP()
};

class CCabFolder :
    public CComCoClass<CCabFolder, &CLSID_CabFolder>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellFolder2,
    public IPersistFolder2
{
protected:
    CComHeapPtr<ITEMIDLIST> m_CurDir;
    IShellFolder2 *m_DeskSF2;
    IShellView *m_ShellView;

public:

    CCabFolder() : m_DeskSF2(NULL), m_ShellView(NULL)
    {
    }

    ~CCabFolder()
    {
        IUnknown_Set((IUnknown**)&m_DeskSF2, NULL);
        IUnknown_Set((IUnknown**)&m_ShellView, NULL);
    }

    HRESULT GetDesktopFolder2()
    {
        DWORD root = 0;
        return m_DeskSF2 ? S_OK : SHBindToParent((LPITEMIDLIST)&root, IID_PPV_ARG(IShellFolder2, &m_DeskSF2), NULL);
    }

    HRESULT ExtractFilesUI(HWND hwnd, IDataObject *pDO);
    HRESULT GetItemDetails(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd, VARIANT *pv);
    int MapSCIDToColumn(const SHCOLUMNID &scid);
    HRESULT CompareID(LPARAM lParam, PCUITEMID_CHILD pidl1, PCUITEMID_CHILD pidl2);

    HRESULT CreateEnum(CEnumIDList **List)
    {
        CEnumIDList *p = CEnumIDList::CreateInstance();
        *List = p;
        return p ? p->Fill(m_CurDir) : E_OUTOFMEMORY;
    }

    // IShellFolder2
    IFACEMETHODIMP GetDefaultSearchGUID(GUID *pguid)
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP EnumSearches(IEnumExtraSearch **ppenum)
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetDefaultColumn(DWORD dwRes, ULONG *pSort, ULONG *pDisplay);

    IFACEMETHODIMP GetDefaultColumnState(UINT iColumn, SHCOLSTATEF *pcsFlags);

    IFACEMETHODIMP GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID *pscid, VARIANT *pv);

    IFACEMETHODIMP GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *psd);

    IFACEMETHODIMP MapColumnToSCID(UINT column, SHCOLUMNID *pscid);

    IFACEMETHODIMP ParseDisplayName(HWND hwndOwner, LPBC pbc, LPOLESTR lpszDisplayName, ULONG *pchEaten, PIDLIST_RELATIVE *ppidl, ULONG *pdwAttributes)
    {
        UNIMPLEMENTED;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP EnumObjects(HWND hwndOwner, DWORD dwFlags, LPENUMIDLIST *ppEnumIDList);

    IFACEMETHODIMP BindToObject(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut);
    
    IFACEMETHODIMP BindToStorage(PCUIDLIST_RELATIVE pidl, LPBC pbcReserved, REFIID riid, LPVOID *ppvOut)
    {
        UNIMPLEMENTED;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2);
    
    IFACEMETHODIMP CreateViewObject(HWND hwndOwner, REFIID riid, LPVOID *ppvOut);
    
    IFACEMETHODIMP GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF *rgfInOut);
    
    IFACEMETHODIMP GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT *prgfInOut, LPVOID *ppvOut);
    
    IFACEMETHODIMP GetDisplayNameOf(PCUITEMID_CHILD pidl, DWORD dwFlags, LPSTRRET pName);
    
    IFACEMETHODIMP SetNameOf(HWND hwndOwner, PCUITEMID_CHILD pidl, LPCOLESTR lpName, DWORD dwFlags, PITEMID_CHILD *pPidlOut)
    {
        return E_NOTIMPL;
    }

    // IPersistFolder2
    IFACEMETHODIMP GetCurFolder(PIDLIST_ABSOLUTE *pidl)
    {
        LPITEMIDLIST curdir = (LPITEMIDLIST)m_CurDir;
        return curdir ? SHILClone(curdir, pidl) : E_UNEXPECTED;
    }

    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidl)
    {
        WCHAR path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path))
        {
            PIDLIST_ABSOLUTE curdir = ILClone(pidl);
            if (curdir)
            {
                m_CurDir.Attach(curdir);
                return S_OK;
            }
            return E_OUTOFMEMORY;
        }
#if DBG
        DbgPrint("%s() => Unable to get FS path from PIDL\n", __FUNCTION__);
#endif
        return E_INVALIDARG;
    }

    IFACEMETHODIMP GetClassID(CLSID *lpClassId)
    {
        *lpClassId = CLSID_CabFolder;
        return S_OK;
    }

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CCabFolder)

    BEGIN_COM_MAP(CCabFolder)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder, IShellFolder)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolder2, IShellFolder2)
        COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder, IPersistFolder)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFolder2, IPersistFolder2)
    END_COM_MAP()
};


/*
ROS notes:
https://jira.reactos.org/browse/CORE-14616 | https://jira.reactos.org/browse/CORE-14615
TODO: test split cabs (with and without 2nd cab existing)
DFM_INVOKECOMMAND is supposed to pass lparam=ici.parametersW
it is also supposed to pass a hwnd, the one you passed to create the IContextMenu
m_pShellBrowser->QueryInterface(riid, ppvObject); must check ptr
*/