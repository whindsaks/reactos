/*
 * PROJECT:     shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Recycle Bin virtual folder callback
 * COPYRIGHT:   Copyright 2026 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 */

#include <precomp.h>

WINE_DEFAULT_DEBUG_CHANNEL(CRecycleBin);

EXTERN_C BOOL IsRecycleBinInternalFile(PCWSTR pszName);

CRecycleBinFolderViewCB::CRecycleBinFolderViewCB()
    : m_pShellView(NULL)
    , m_pRecycleBin(NULL)
    , m_nChangeNotif(0)
{
}

CRecycleBinFolderViewCB::~CRecycleBinFolderViewCB()
{
    if (m_nChangeNotif)
        SHChangeNotifyDeregister(m_nChangeNotif);
}

void CRecycleBinFolderViewCB::Initialize(CRecycleBin *pRecycleBin, IShellView *psv, LPCITEMIDLIST pidlParent)
{
    m_pRecycleBin = pRecycleBin;
    m_pShellView = psv;
    m_pidlParent.Attach(ILClone(pidlParent));
}

static bool IsInternalRecyclerItem(PCWSTR pszPath)
{
    PCWSTR pszName = PathFindFileNameW(pszPath);
    return IsRecycleBinInternalFile(pszName) || !_wcsicmp(pszName, L"desktop.ini");
}

static bool IsInternalRecyclerItem(PIDLIST_ABSOLUTE pidl)
{
    WCHAR szPath[MAX_PATH];
    return SHGetPathFromIDListW(pidl, szPath) && IsInternalRecyclerItem(szPath);
}

HRESULT CRecycleBinFolderViewCB::RegisterChangeNotify(HWND hwndView)
{
    if (!hwndView)
    {
        WARN("!hwndView\n");
        return E_FAIL;
    }

    LPITEMIDLIST pidls[RECYCLEBINMAXDRIVECOUNT] = { NULL };
    SHChangeNotifyEntry entries[RECYCLEBINMAXDRIVECOUNT] = {};

    // Populate entries
    INT iEntry = 0;
    for (INT iDrive = 0; iDrive < RECYCLEBINMAXDRIVECOUNT; ++iDrive)
    {
        WCHAR szBinPath[MAX_PATH];
        HRESULT hr = GetRecycleBinPathFromDriveNumber(iDrive, szBinPath);
        if (FAILED(hr))
            continue;
        LPITEMIDLIST pidl = ILCreateFromPathW(szBinPath);
        if (!pidl)
            continue;
        entries[iEntry].pidl = pidls[iEntry] = pidl;
        entries[iEntry].fRecursive = FALSE;
        ++iEntry;
    }

    // Register
    const DWORD dwFlags = SHCNRF_InterruptLevel | SHCNRF_ShellLevel | SHCNRF_NewDelivery;
    const DWORD dwEvents =
        SHCNE_CREATE |
        SHCNE_DELETE |
        SHCNE_RENAMEITEM |
        SHCNE_UPDATEITEM |
        SHCNE_MKDIR |
        SHCNE_RMDIR |
        SHCNE_RENAMEFOLDER |
        SHCNE_UPDATEDIR |
        SHCNE_ASSOCCHANGED;
    m_nChangeNotif = SHChangeNotifyRegister(hwndView, dwFlags, dwEvents, SHV_CHANGE_NOTIFY,
                                            iEntry, entries);
    if (!m_nChangeNotif)
        WARN("SHChangeNotifyRegister failed\n");

    // Clean up
    while (iEntry > 0)
        ILFree(pidls[--iEntry]);

    return m_nChangeNotif ? S_OK : E_FAIL;
}

HRESULT CRecycleBinFolderViewCB::ParsePidl(PIDLIST_ABSOLUTE pidlAbs, PITEMID_CHILD &pidlChild)
{
    WCHAR szPath[MAX_PATH];
    if (!SHGetPathFromIDListW(pidlAbs, szPath))
    {
        ERR("!SHGetPathFromIDListW\n");
        return E_FAIL;
    }
    return m_pRecycleBin->ParseRecycleBinPath(szPath, NULL, &pidlChild, NULL);
}

HRESULT CRecycleBinFolderViewCB::TranslatePidl(LPITEMIDLIST *ppidlNew, PIDLIST_ABSOLUTE pidl)
{
    ATLASSERT(ppidlNew);
    *ppidlNew = NULL;

    PITEMID_CHILD pidlChild;
    HRESULT hr = ParsePidl(pidl, pidlChild);
    if (FAILED(hr))
        return hr;

    *ppidlNew = ILCombine(m_pidlParent, pidlChild);
    return *ppidlNew ? S_OK : E_OUTOFMEMORY;
}

HRESULT CRecycleBinFolderViewCB::TranslateTwoPIDLs(PIDLIST_ABSOLUTE* pidls)
{
    ATLASSERT(pidls);

    HRESULT hr, hrRet = S_OK;
    if (pidls[0])
    {
        m_pidls[0].Free();
        hrRet = hr = TranslatePidl(&m_pidls[0], pidls[0]);
        if (!FAILED_UNEXPECTEDLY(hr))
            pidls[0] = m_pidls[0];
    }
    if (pidls[1])
    {
        m_pidls[1].Free();
        hr = TranslatePidl(&m_pidls[1], pidls[1]);
        if (!FAILED_UNEXPECTEDLY(hr))
            pidls[1] = m_pidls[1];
        else
            hrRet = hr;
    }
    return hrRet;
}

HRESULT CRecycleBinFolderViewCB::HandleFSNotify(UINT Event, PIDLIST_ABSOLUTE *pidls)
{
    ATLASSERT(pidls);

    PITEMID_CHILD pidlChild;

    {WCHAR b[MAX_PATH]={};SHGetPathFromIDListW(pidls[0],b);DbgPrint("HandleFSNotify %#x:%ls\n", Event,b);}
    switch (Event)
    {
        case SHCNE_UPDATEIMAGE: // No PIDLs to translate, just let the view deal with this event
            return E_NOTIMPL;
        case SHCNE_CREATE:
        case SHCNE_MKDIR:
            // We get two create events.
            // The first from the filesystem we ignore because the deleted item is not yet in the database.
            // The second is genrated by a call to CRecycleBin_NotifyRecycled and will pass IsValidItem.
            pidlChild = (PITEMID_CHILD)ILFindLastID(pidls[0]);
            if (!m_pRecycleBin->IsValidItem(pidlChild) || SUCCEEDED(AddObject(pidlChild)))
                 return S_FALSE;
            break;
        case SHCNE_DELETE:
        case SHCNE_RMDIR:
            pidlChild = (PITEMID_CHILD)ILFindLastID(pidls[0]);
            if (m_pRecycleBin->IsValidItem(pidlChild))
            {
                if (SUCCEEDED(RemoveObject(pidlChild)))
                    return S_FALSE;
            }
            break;
#if 0   // This is optional, we can just let the view handle it normally
        case SHCNE_UPDATEDIR:
            if (m_pShellView->Refresh() == S_OK)
                return S_FALSE;
            break;
#endif
    }

    if (IsInternalRecyclerItem(pidls[0]))
        return S_FALSE; // Eat INFO2 and desktop.ini changes

    return TranslateTwoPIDLs(pidls);
}

STDMETHODIMP
CRecycleBinFolderViewCB::MessageSFVCB(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case SFVM_QUERYFSNOTIFY: // Register change notification
        {
            // Now, we can get the view window
            ATLASSERT(m_pShellView);
            HWND hwndView;
            HRESULT hr = m_pShellView->GetWindow(&hwndView);
            if (FAILED_UNEXPECTEDLY(hr))
                return hr;
            RegisterChangeNotify(hwndView);
            return S_OK;
        }
        case SFVM_FSNOTIFY: // Change notification
            return HandleFSNotify((UINT)lParam & SHCNE_ALLEVENTS, (PIDLIST_ABSOLUTE*)wParam);
        case SFVM_WINDOWCLOSING:
            return !SHChangeNotifyDeregister(m_nChangeNotif);
    }
    return E_NOTIMPL;
}
