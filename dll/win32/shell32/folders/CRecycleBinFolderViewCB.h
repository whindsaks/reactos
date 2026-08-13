/*
 * PROJECT:     shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Recycle Bin virtual folder callback
 * COPYRIGHT:   Copyright 2026 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 */

#pragma once

class CRecycleBinFolderViewCB
    : public CComObjectRootEx<CComMultiThreadModelNoCS>
    , public IShellFolderViewCB
{
    IShellView *m_pShellView; // Not ref-counted!
    CRecycleBin *m_pRecycleBin;
    ULONG m_nChangeNotif; // Change notification handle
    CComHeapPtr<ITEMIDLIST> m_pidlParent;
    CComHeapPtr<ITEMIDLIST> m_pidls[2];

    HRESULT RegisterChangeNotify(HWND hwndView);
    HRESULT ParsePidl(PIDLIST_ABSOLUTE pidlAbs, PITEMID_CHILD &pidlChild);
    HRESULT TranslatePidl(LPITEMIDLIST *ppidlNew, PIDLIST_ABSOLUTE pidl);
    HRESULT TranslateTwoPIDLs(PIDLIST_ABSOLUTE* pidls);
    HRESULT HandleFSNotify(UINT Event, PIDLIST_ABSOLUTE *pidls);

    HRESULT AddObject(PITEMID_CHILD pidlChild)
    {
        CComPtr<IShellFolderView> pSFV;
        HRESULT hr = m_pShellView ? m_pShellView->QueryInterface(IID_PPV_ARG(IShellFolderView, &pSFV)) : E_UNEXPECTED;
        UINT idx;
        return SUCCEEDED(hr) ? pSFV->AddObject(pidlChild, &idx) : hr;
    }

    HRESULT RemoveObject(PITEMID_CHILD pidlChild)
    {
        CComPtr<IShellFolderView> pSFV;
        HRESULT hr = m_pShellView ? m_pShellView->QueryInterface(IID_PPV_ARG(IShellFolderView, &pSFV)) : E_UNEXPECTED;
        UINT idx;
        return SUCCEEDED(hr) ? pSFV->RemoveObject(pidlChild, &idx) : hr;
    }

public:
    CRecycleBinFolderViewCB();
    virtual ~CRecycleBinFolderViewCB();
    void Initialize(CRecycleBin *pRecycleBin, IShellView *psv, LPCITEMIDLIST pidlParent);

    // IShellFolderViewCB
    STDMETHODIMP MessageSFVCB(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CRecycleBinFolderViewCB)
    BEGIN_COM_MAP(CRecycleBinFolderViewCB)
        COM_INTERFACE_ENTRY_IID(IID_IShellFolderViewCB, IShellFolderViewCB)
    END_COM_MAP()
};
