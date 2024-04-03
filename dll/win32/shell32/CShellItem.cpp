/*
 * IShellItem implementation
 *
 * Copyright 2008 Vincent Povirk for CodeWeavers
 * Copyright 2009 Andrew Hill
 * Copyright 2013 Katayama Hirofumi MZ
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

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

EXTERN_C HRESULT WINAPI SHCreateShellItem(PCIDLIST_ABSOLUTE pidlParent,
    IShellFolder *psfParent, PCUITEMID_CHILD pidl, IShellItem **ppsi);

CShellItem::CShellItem() :
    m_pidl(NULL)
{
}

CShellItem::~CShellItem()
{
    ILFree(m_pidl);
}

HRESULT CShellItem::get_parent_pidl(LPITEMIDLIST *parent_pidl)
{
    *parent_pidl = ILClone(m_pidl);
    if (*parent_pidl)
    {
        if (ILRemoveLastID(*parent_pidl))
            return S_OK;
        else
        {
            ILFree(*parent_pidl);
            *parent_pidl = NULL;
            return E_INVALIDARG;
        }
    }
    else
    {
        *parent_pidl = NULL;
        return E_OUTOFMEMORY;
    }
}

HRESULT CShellItem::get_parent_shellfolder(IShellFolder **ppsf)
{
    HRESULT hr;
    LPITEMIDLIST parent_pidl;
    CComPtr<IShellFolder>        desktop;

    hr = get_parent_pidl(&parent_pidl);
    if (SUCCEEDED(hr))
    {
        hr = SHGetDesktopFolder(&desktop);
        if (SUCCEEDED(hr))
            hr = desktop->BindToObject(parent_pidl, NULL, IID_PPV_ARG(IShellFolder, ppsf));
        ILFree(parent_pidl);
    }

    return hr;
}

HRESULT CShellItem::get_shellfolder(IBindCtx *pbc, REFIID riid, void **ppvOut)
{
    CComPtr<IShellFolder> psf;
    CComPtr<IShellFolder> psfDesktop;
    HRESULT ret;

    ret = SHGetDesktopFolder(&psfDesktop);
    if (FAILED_UNEXPECTEDLY(ret))
        return ret;

    if (_ILIsDesktop(m_pidl))
        psf = psfDesktop;
    else
    {
        ret = psfDesktop->BindToObject(m_pidl, pbc, IID_PPV_ARG(IShellFolder, &psf));
        if (FAILED_UNEXPECTEDLY(ret))
            return ret;
    }

    return psf->QueryInterface(riid, ppvOut);
}

HRESULT WINAPI CShellItem::BindToHandler(IBindCtx *pbc, REFGUID rbhid, REFIID riid, void **ppvOut)
{
    HRESULT ret;
    TRACE("(%p, %p,%s,%p,%p)\n", this, pbc, shdebugstr_guid(&rbhid), riid, ppvOut);

    *ppvOut = NULL;
    if (IsEqualGUID(rbhid, BHID_SFObject))
    {
        return get_shellfolder(pbc, riid, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_SFUIObject))
    {
        CComPtr<IShellFolder> psf_parent;
        if (_ILIsDesktop(m_pidl))
            ret = SHGetDesktopFolder(&psf_parent);
        else
            ret = get_parent_shellfolder(&psf_parent);
        if (FAILED_UNEXPECTEDLY(ret))
            return ret;

        LPCITEMIDLIST pidl = ILFindLastID(m_pidl);
        return psf_parent->GetUIObjectOf(NULL, 1, &pidl, riid, NULL, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_DataObject))
    {
        return BindToHandler(pbc, BHID_SFUIObject, IID_IDataObject, ppvOut);
    }
    else if (IsEqualGUID(rbhid, BHID_SFViewObject))
    {
        CComPtr<IShellFolder> psf;
        ret = get_shellfolder(NULL, IID_PPV_ARG(IShellFolder, &psf));
        if (FAILED_UNEXPECTEDLY(ret))
            return ret;

        return psf->CreateViewObject(NULL, riid, ppvOut);
    }

    FIXME("Unsupported BHID %s.\n", debugstr_guid(&rbhid));

    return MK_E_NOOBJECT;
}

HRESULT WINAPI CShellItem::GetParent(IShellItem **ppsi)
{
    HRESULT hr;
    LPITEMIDLIST parent_pidl;

    TRACE("(%p,%p)\n", this, ppsi);

    hr = get_parent_pidl(&parent_pidl);
    if (SUCCEEDED(hr))
    {
        hr = SHCreateShellItem(NULL, NULL, parent_pidl, ppsi);
        ILFree(parent_pidl);
    }

    return hr;
}

HRESULT WINAPI CShellItem::GetDisplayName(SIGDN sigdnName, LPWSTR *ppszName)
{
    return SHGetNameFromIDList(m_pidl, sigdnName, ppszName);
}

HRESULT WINAPI CShellItem::GetAttributes(SFGAOF sfgaoMask, SFGAOF *psfgaoAttribs)
{
    CComPtr<IShellFolder>        parent_folder;
    LPCITEMIDLIST child_pidl;
    HRESULT hr;

    TRACE("(%p,%x,%p)\n", this, sfgaoMask, psfgaoAttribs);

    if (_ILIsDesktop(m_pidl))
        hr = SHGetDesktopFolder(&parent_folder);
    else
        hr = get_parent_shellfolder(&parent_folder);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    child_pidl = ILFindLastID(m_pidl);
    *psfgaoAttribs = sfgaoMask;
    hr = parent_folder->GetAttributesOf(1, &child_pidl, psfgaoAttribs);
    *psfgaoAttribs &= sfgaoMask;

    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    return (sfgaoMask == *psfgaoAttribs) ? S_OK : S_FALSE;
}

HRESULT WINAPI CShellItem::Compare(IShellItem *oth, SICHINTF hint, int *piOrder)
{
    HRESULT hr;
    CComPtr<IPersistIDList>      pIDList;
    CComPtr<IShellFolder>        psfDesktop;
    LPITEMIDLIST pidl;

    TRACE("(%p,%p,%x,%p)\n", this, oth, hint, piOrder);

    if (piOrder == NULL || oth == NULL)
        return E_POINTER;

    hr = oth->QueryInterface(IID_PPV_ARG(IPersistIDList, &pIDList));
    if (SUCCEEDED(hr))
    {
        hr = pIDList->GetIDList(&pidl);
        if (SUCCEEDED(hr))
        {
            hr = SHGetDesktopFolder(&psfDesktop);
            if (SUCCEEDED(hr))
            {
                hr = psfDesktop->CompareIDs(hint, m_pidl, pidl);
                *piOrder = (int)(short)SCODE_CODE(hr);
            }
            ILFree(pidl);
        }
    }

    if(FAILED(hr))
        return hr;

    if(*piOrder)
        return S_FALSE;
    else
        return S_OK;
}

HRESULT WINAPI CShellItem::GetClassID(CLSID *pClassID)
{
    TRACE("(%p,%p)\n", this, pClassID);

    *pClassID = CLSID_ShellItem;
    return S_OK;
}

HRESULT WINAPI CShellItem::SetIDList(PCIDLIST_ABSOLUTE pidlx)
{
    LPITEMIDLIST new_pidl;

    TRACE("(%p,%p)\n", this, pidlx);

    new_pidl = ILClone(pidlx);
    if (new_pidl)
    {
        ILFree(m_pidl);
        m_pidl = new_pidl;
        return S_OK;
    }
    else
        return E_OUTOFMEMORY;
}

HRESULT WINAPI CShellItem::GetIDList(PIDLIST_ABSOLUTE *ppidl)
{
    TRACE("(%p,%p)\n", this, ppidl);

    *ppidl = ILClone(m_pidl);
    if (*ppidl)
        return S_OK;
    else
        return E_OUTOFMEMORY;
}

HRESULT WINAPI SHCreateShellItem(PCIDLIST_ABSOLUTE pidlParent,
    IShellFolder *psfParent, PCUITEMID_CHILD pidl, IShellItem **ppsi)
{
    HRESULT hr;
    CComPtr<IShellItem> newShellItem;
    LPITEMIDLIST new_pidl;
    CComPtr<IPersistIDList>            newPersistIDList;

    TRACE("(%p,%p,%p,%p)\n", pidlParent, psfParent, pidl, ppsi);

    *ppsi = NULL;

    if (!pidl)
        return E_INVALIDARG;

    if (pidlParent || psfParent)
    {
        LPITEMIDLIST temp_parent = NULL;
        if (!pidlParent)
        {
            CComPtr<IPersistFolder2>    ppf2Parent;

            if (FAILED(psfParent->QueryInterface(IID_PPV_ARG(IPersistFolder2, &ppf2Parent))))
            {
                FIXME("couldn't get IPersistFolder2 interface of parent\n");
                return E_NOINTERFACE;
            }

            if (FAILED(ppf2Parent->GetCurFolder(&temp_parent)))
            {
                FIXME("couldn't get parent PIDL\n");
                return E_NOINTERFACE;
            }

            pidlParent = temp_parent;
        }

        new_pidl = ILCombine(pidlParent, pidl);
        ILFree(temp_parent);

        if (!new_pidl)
            return E_OUTOFMEMORY;
    }
    else
    {
        new_pidl = ILClone(pidl);
        if (!new_pidl)
            return E_OUTOFMEMORY;
    }

    hr = CShellItem::_CreatorClass::CreateInstance(NULL, IID_PPV_ARG(IShellItem, &newShellItem));
    if (FAILED(hr))
    {
        ILFree(new_pidl);
        return hr;
    }
    hr = newShellItem->QueryInterface(IID_PPV_ARG(IPersistIDList, &newPersistIDList));
    if (FAILED(hr))
    {
        ILFree(new_pidl);
        return hr;
    }
    hr = newPersistIDList->SetIDList(new_pidl);
    if (FAILED(hr))
    {
        ILFree(new_pidl);
        return hr;
    }
    ILFree(new_pidl);

    *ppsi = newShellItem.Detach();

    return hr;
}

static HRESULT SHELL32_CreateShellItem(PCIDLIST_ABSOLUTE pidlParent, IShellFolder *psfParent, PCUITEMID_CHILD pidl, REFIID riid, void **ppv)
{
    CComPtr<IShellItem> item;
    HRESULT hr = SHCreateShellItem(pidlParent, psfParent, pidl, &item);
    if (SUCCEEDED(hr))
    {
        hr = item->QueryInterface(riid, ppv);
    }
    return hr;
}

static HRESULT DataObject_GetLockedGlobal(IDataObject *pdo, CLIPFORMAT cf, STGMEDIUM *psm, LPVOID *ptr)
{
    FORMATETC fmt = { cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    psm->tymed = TYMED_NULL;
    HRESULT hr = pdo->GetData(&fmt, psm);
    if (SUCCEEDED(hr))
    {
        LPVOID blob = GlobalLock(psm->hGlobal);
        *ptr = blob;
        if (blob)
            return S_OK;
        ReleaseStgMedium(psm);
        psm->tymed = TYMED_NULL;
    }
    return hr;
}

class CShellItemArray :
    public CComCoClass<CShellItemArray, &CLSID_NULL>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IShellItemArray
{
private:
    IUnknown *m_Ref;
    IShellFolder *m_SF;
    PCUIDLIST_ABSOLUTE m_Parent;
    PCUIDLIST_RELATIVE *m_Items;
    UINT m_count;
    STGMEDIUM m_Stgm; // CIDA

    HRESULT Initialize(CIDA const *cida);

public:
DECLARE_NO_REGISTRY()
DECLARE_NOT_AGGREGATABLE(CShellItemArray)

BEGIN_COM_MAP(CShellItemArray)
    COM_INTERFACE_ENTRY_IID(IID_IShellItemArray, IShellItemArray)
END_COM_MAP()

    CShellItemArray() : m_Ref(NULL), m_SF(NULL), m_Parent(NULL), m_Items(NULL), m_count(0)
    {
        m_Stgm.tymed = TYMED_NULL;
    }

    ~CShellItemArray()
    {
        IUnknown_Set((IUnknown**)&m_Ref, NULL);
        IUnknown_Set((IUnknown**)&m_SF, NULL);
        ReleaseStgMedium(&m_Stgm);
        SHFree(m_Items);
    }

    // IShellItemArray
    STDMETHODIMP GetCount(DWORD *pdwNumItems) override
    {
        *pdwNumItems = m_count;
        return S_OK;
    }

    STDMETHODIMP BindToHandler(IBindCtx *pbc, REFGUID bhid, REFIID riid, void **ppvOut) override
    {
        UNIMPLEMENTED;
        *ppvOut = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP EnumItems(IEnumShellItems **ppenumShellItems) override
    {
        UNIMPLEMENTED;
        *ppenumShellItems = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetPropertyDescriptionList(REFPROPERTYKEY keyType, REFIID riid, void **ppv) override
    {
        UNIMPLEMENTED;
        *ppv = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetPropertyStore(GETPROPERTYSTOREFLAGS flags, REFIID riid, void **ppv) override
    {
        UNIMPLEMENTED;
        *ppv = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetAttributes(SIATTRIBFLAGS AttribFlags, SFGAOF sfgaoMask, SFGAOF *psfgaoAttribs) override
    {
        UNIMPLEMENTED;
        *psfgaoAttribs = 0;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetItemAt(DWORD dwIndex, IShellItem **ppsi) override
    {
        return dwIndex < m_count ? SHCreateShellItem(m_Parent, m_SF, m_Items[dwIndex], ppsi) : E_FAIL;
    }

    static HRESULT CreateInstance(CShellItemArray *&array)
    {
        IShellItemArray *p;
        HRESULT hr = _CreatorClass::CreateInstance(NULL, IID_PPV_ARG(IShellItemArray, &p));
        array = static_cast<CShellItemArray*>(p);
        return hr;
    }

    static HRESULT CreateFromDataObject(IDataObject *pdo, IShellItemArray **ppsi);
    static HRESULT Create(PCIDLIST_ABSOLUTE pidlParent, IShellFolder *psf, UINT cidl, PCUITEMID_CHILD_ARRAY ppidl, IShellItemArray **ppsi);
};

HRESULT CShellItemArray::Initialize(CIDA const *cida)
{
    m_count = cida->cidl;
    m_Items = (PCUIDLIST_RELATIVE*)SHAlloc(m_count * sizeof(PCUIDLIST_RELATIVE));
    if (!m_Items)
        return E_OUTOFMEMORY;
    m_Parent = HIDA_GetPIDLFolder(cida); // Note: Not a clone
    for (UINT i = 0; i < m_count; ++i)
    {
        m_Items[i] = HIDA_GetPIDLItem(cida, i); // Note: Not a clone
    }
    return S_OK;
}

HRESULT CShellItemArray::CreateFromDataObject(IDataObject *pdo, IShellItemArray **ppsi)
{
    if (!pdo)
        return E_POINTER;
    if (!g_cfHIDA)
        g_cfHIDA = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_SHELLIDLISTW);

    CShellItemArray *array;
    HRESULT hr = CreateInstance(array);
    *ppsi = static_cast<IShellItemArray*>(array);
    if (SUCCEEDED(hr))
    {
        CIDA const *cida;
        hr = DataObject_GetLockedGlobal(pdo, g_cfHIDA, &array->m_Stgm, (void**)&cida);
        if (SUCCEEDED(hr))
        {
            hr = array->Initialize(cida);
            if (SUCCEEDED(hr))
            {
                IUnknown_Set((IUnknown**)&array->m_Ref, pdo); // MSDN says we are supposed to hold a reference
                return S_OK;
            }
        }
        array->Release();
    }
    return hr;
}

HRESULT CShellItemArray::Create(PCIDLIST_ABSOLUTE pidlParent, IShellFolder *psf, UINT cidl, PCUITEMID_CHILD_ARRAY ppidl, IShellItemArray **ppsi)
{
    CShellItemArray *array;
    HRESULT hr = CreateInstance(array);
    *ppsi = static_cast<IShellItemArray*>(array);
    if (SUCCEEDED(hr))
    {
        IUnknown_Set((IUnknown**)&array->m_SF, psf);
        hr = E_OUTOFMEMORY;
        HGLOBAL hida = RenderSHELLIDLIST(const_cast<LPITEMIDLIST>(pidlParent), (LPITEMIDLIST*)ppidl, cidl);
        if (hida)
        {
            array->m_Stgm.tymed = TYMED_HGLOBAL;
            array->m_Stgm.hGlobal = hida;
            array->m_Stgm.pUnkForRelease = NULL;
            CIDA *cida = (CIDA*)GlobalLock(hida);
            if (cida)
            {
                hr = array->Initialize(cida);
                if (SUCCEEDED(hr))
                {
                    return S_OK;
                }
            }
        }
        array->Release();
    }
    return hr;
}

/*************************************************************************
 * SHCreateShellItemArrayFromDataObject [SHELL32.@]
 */
HRESULT WINAPI SHCreateShellItemArrayFromDataObject(IDataObject *pdo, REFIID riid, void**ppv)
{
    IShellItemArray *array;
    HRESULT hr = CShellItemArray::CreateFromDataObject(pdo, &array);
    if (SUCCEEDED(hr))
    {
        hr = array->QueryInterface(riid, ppv);
        array->Release();
    }
    return hr;
}

/*************************************************************************
 * SHCreateShellItemArray [SHELL32.@]
 */
HRESULT WINAPI SHCreateShellItemArray(PCIDLIST_ABSOLUTE pidlParent, IShellFolder *psf, UINT cidl, PCUITEMID_CHILD_ARRAY ppidl, IShellItemArray **ppsiItemArray)
{
    HRESULT hr = S_OK;
    PIDLIST_ABSOLUTE pidl = NULL;
    if (!pidlParent)
    {
        hr = psf ? SHGetIDListFromObject(psf, &pidl) : E_POINTER;
        if (SUCCEEDED(hr))
        {
            pidlParent = pidl;
        }
    }
    if (SUCCEEDED(hr))
    {
        hr = CShellItemArray::Create(pidlParent, psf, cidl, ppidl, ppsiItemArray);
    }
    ILFree(pidl);
    return hr;
}

/*************************************************************************
 * SHCreateShellItemArrayFromIDLists [SHELL32.@]
 */
HRESULT WINAPI SHCreateShellItemArrayFromIDLists(UINT cidl, PCIDLIST_ABSOLUTE_ARRAY rgpidl, IShellItemArray **ppsiItemArray)
{
    DWORD root = 0;
    return SHCreateShellItemArray((PCIDLIST_ABSOLUTE)&root, NULL, cidl, rgpidl, ppsiItemArray);
}

/*************************************************************************
 * SHCreateShellItemArrayFromShellItem [SHELL32.@]
 */
HRESULT WINAPI SHCreateShellItemArrayFromShellItem(IShellItem *psi, REFIID riid, void **ppv)
{
    PIDLIST_ABSOLUTE pidl;
    HRESULT hr = SHGetIDListFromObject(psi, &pidl);
    if (SUCCEEDED(hr))
    {
        IShellItemArray *array;
        hr = SHCreateShellItemArrayFromIDLists(1, &pidl, &array);
        if (SUCCEEDED(hr))
        {
            hr = array->QueryInterface(riid, ppv);
            array->Release();
        }
        ILFree(pidl);
    }
    return hr;
}

static HRESULT SHELL32_GetIDListFromDataObject(IDataObject *pdtobj, DATAOBJ_GET_ITEM_FLAGS dwFlags, PIDLIST_ABSOLUTE *ppidl)
{
    return E_NOTIMPL;
}

/*************************************************************************
 * SHGetItemFromDataObject [SHELL32.@]
 */
HRESULT WINAPI SHGetItemFromDataObject(IDataObject *pdtobj, DATAOBJ_GET_ITEM_FLAGS dwFlags, REFIID riid, void **ppv)
{
    PIDLIST_ABSOLUTE pidl;
    HRESULT hr = SHELL32_GetIDListFromDataObject(pdtobj, dwFlags, &pidl);
    if (SUCCEEDED(hr))
    {
        hr = SHELL32_CreateShellItem(NULL, NULL, pidl, riid, ppv);
        ILFree(pidl);
    }
    return hr;
}

/*************************************************************************
 * SHGetItemFromObject [SHELL32.@]
 */
SHSTDAPI SHGetItemFromObject(IUnknown *punk, REFIID riid, void **ppv)
{
    PIDLIST_ABSOLUTE pidl;
    HRESULT hr = SHGetIDListFromObject(punk, &pidl);
    if (SUCCEEDED(hr))
    {
        hr = SHELL32_CreateShellItem(NULL, NULL, pidl, riid, ppv);
        ILFree(pidl);
    }
    return hr;
}
