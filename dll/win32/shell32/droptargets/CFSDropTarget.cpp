
/*
 * file system folder drop target
 *
 * Copyright 1997             Marcus Meissner
 * Copyright 1998, 1999, 2002 Juergen Schmied
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

WINE_DEFAULT_DEBUG_CHANNEL (shell);

static bool IsInvalidFsNameCharacter(WCHAR c)
{
    return c < ' ' || c == ':' || c == '<' || c == '>' || c == '|' ||
           c == '?' || c == '*' || c == '\\' || c == '/' || c == 127;
}

static void RemoveInvalidFsNameCharacters(PWSTR str)
{
    for (PWSTR src = str, dst = src;; ++src, ++dst)
    {
        while (IsInvalidFsNameCharacter(*src) && *src)
            ++src;
        *dst = *src;
        if (!*dst)
            break;
    }
}

static HRESULT SHELL_GetNameRetAndAttributes(PCIDLIST_ABSOLUTE pidl, UINT shgdn, STRRET *pSR, DWORD *pAtt)
{
    IShellFolder *pSF;
    PCUITEMID_CHILD child;
    HRESULT hr = SHBindToParent(pidl, IID_PPV_ARG(IShellFolder, &pSF), &child);
    if (FAILED(hr))
        return hr;
    if (pAtt && FAILED(hr = pSF->GetAttributesOf(1, &child, pAtt)))
        *pAtt = 0;
    if (SUCCEEDED(hr) && pSR)
    {
        hr = pSF->GetDisplayNameOf(child, shgdn, pSR);
        if (SUCCEEDED(hr) && pSR->uType == STRRET_OFFSET)
        {
            hr = StrRetToStrW(pSR, child, &pSR->pOleStr);
            if (SUCCEEDED(hr))
                pSR->uType = STRRET_WSTR;
        }
    }
    pSF->Release();
    return hr;
}

static HRESULT SHELL32_GetNewLinkPath(LPCWSTR Directory, LPCWSTR BaseName, PWSTR *Output, UINT shgnli)
{
    PCWSTR prefix = L"";
    WCHAR ext[1 + 3 + 1], number[42 + 1], sep[1 + 1], prefixbuf[100];
    if ((shgnli & SHGNLI_PREFIXNAME) && LoadStringW(shell32_hInstance, IDS_LNK_FILE, prefixbuf, _countof(prefixbuf)))
        prefix = prefixbuf;
    SIZE_T cchDir = lstrlenW(Directory);
    CComHeapPtr<WCHAR> full;
    if (!full.Reallocate(cchDir + 1 + lstrlenW(prefix) + lstrlenW(BaseName) + _countof(number) + 1 + 3))
        return E_OUTOFMEMORY;
    *ext = *number = *sep = UNICODE_NULL;
    if (!(shgnli & SHGNLI_NOLNK))
        lstrcpyW(ext, L".lnk");
#ifdef SHGNLI_USEURLEXT
    if (shgnli & SHGNLI_USEURLEXT)
        lstrcpyW(ext, L".url");
#endif
    if (cchDir && Directory[cchDir - 1] != '\\' && Directory[cchDir - 1] != '/')
        lstrcpyW(sep, L"\\");
    for (UINT i = 0;;)
    {
        if (i)
            wsprintfW(number, L" (%u)", i);
        if (!++i)
            return E_FAIL;
        wsprintfW(full, L"%s%s%s%s%s%s", Directory, sep, prefix, BaseName, number, ext);
        RemoveInvalidFsNameCharacters(full + cchDir + !!*sep);
        if ((shgnli & SHGNLI_NOUNIQUE) || !PathFileExistsW(full))
            break;
    }
    *Output = full.Detach();
    return S_OK;
}

static HRESULT SHELL32_CreateLink(HWND hWnd, LPCWSTR Directory, PCIDLIST_ABSOLUTE Target)
{
    WCHAR Name[MAX_PATH];
    DWORD Att = SFGAO_LINK | SFGAO_FILESYSTEM;
    STRRET sr;
    HRESULT hr = SHELL_GetNameRetAndAttributes(Target, SHGDN_NORMAL, &sr, &Att);
    if (SUCCEEDED(hr))
        hr = StrRetToBufW(&sr, NULL, Name, _countof(Name));
    if (FAILED(hr))
        return hr;

    PWSTR Link;
    hr = SHELL32_GetNewLinkPath(Directory, Name, &Link, 0);
    if (FAILED(hr))
        return hr;

    IShellLinkW *pSL;
    if (Att & SFGAO_LINK)
    {
        IShellFolder *pSF;
        PCUITEMID_CHILD child;
        hr = SHBindToParent(Target, IID_PPV_ARG(IShellFolder, &pSF), &child);
        if (SUCCEEDED(hr))
        {
            hr = pSF->GetUIObjectOf(hWnd, 1, &child, IID_IShellLinkW, NULL, (void**)&pSL);
            if (FAILED(hr))
                hr = pSF->GetUIObjectOf(hWnd, 1, &child, IID_IShellLinkA, NULL, (void**)&pSL);
            pSF->Release();
        }
    }
    else
    {
        hr = CShellLink::_CreatorClass::CreateInstance(NULL, IID_PPV_ARG(IShellLinkW, &pSL));
        if (SUCCEEDED(hr))
            hr = pSL->SetIDList(Target);
    }
    if (SUCCEEDED(hr))
    {
        IPersistFile *pPF;
        hr = pSL->QueryInterface(IID_PPV_ARG(IPersistFile, &pPF));
        if (SUCCEEDED(hr))
        {
            hr = pPF->Save(Link, FALSE);
            if (SUCCEEDED(hr))
                SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, Link, NULL);
            pPF->Release();
        }
        pSL->Release();
    }
    SHFree(Link);
    return hr;
}

struct COleWindow : CComObjectRootEx<CComMultiThreadModelNoCS>, IOleWindow
{
    COleWindow() {}
    HRESULT Initialize(HWND hWnd) { m_hWnd = hWnd; return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE GetWindow(HWND *pOut) { *pOut = m_hWnd; return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL fEnterMode) { return E_NOTIMPL; }
    DECLARE_NOT_AGGREGATABLE(COleWindow)
    DECLARE_PROTECT_FINAL_CONSTRUCT()
    BEGIN_COM_MAP(COleWindow)
    COM_INTERFACE_ENTRY_IID(IID_IOleWindow, IOleWindow)
    END_COM_MAP()
    static IOleWindow* CreateInstance(HWND hWnd)
    {
        IOleWindow *pOW = NULL;
        ShellObjectCreatorInit<COleWindow>(hWnd, IID_PPV_ARG(IOleWindow, &pOW));
        return pOW;
    }
    HWND m_hWnd;
}; 

EXTERN_C HRESULT SHELL32_SFCreateLinks(HWND hWnd, IUnknown *pSite, IShellFolder *pDestination, IDataObject *pTargets)
{
    CComPtr<IOleWindow> pOW;
    CComPtr<IDropTarget> pDT;
    HRESULT hr = pDestination->CreateViewObject(NULL, IID_PPV_ARG(IDropTarget, &pDT));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    if (IsWindowVisible(hWnd))
        pSite = pOW = COleWindow::CreateInstance(hWnd);
    IUnknown_SetSite(pDT, pSite);
    DWORD effect = DROPEFFECT_LINK;
    hr = SHSimulateDrop(pDT, pTargets, MK_CONTROL|MK_SHIFT, NULL, &effect);
    IUnknown_SetSite(pDT, NULL);
    return hr;
}


/****************************************************************************
 * CFSDropTarget::_CopyItems
 *
 * copies or moves items to this folder
 */
HRESULT CFSDropTarget::_CopyItems(IShellFolder * pSFFrom, UINT cidl,
                                  LPCITEMIDLIST * apidl, BOOL bCopy)
{
    HRESULT ret;
    WCHAR wszDstPath[MAX_PATH + 1] = {0};
    PWCHAR pwszSrcPathsList = (PWCHAR) HeapAlloc(GetProcessHeap(), 0, MAX_PATH * sizeof(WCHAR) * cidl + 1);
    if (!pwszSrcPathsList)
        return E_OUTOFMEMORY;

    PWCHAR pwszListPos = pwszSrcPathsList;
    STRRET strretFrom;
    SHFILEOPSTRUCTW fop;
    BOOL bRenameOnCollision = FALSE;

    /* Build a double null terminated list of C strings from source paths */
    for (UINT i = 0; i < cidl; i++)
    {
        ret = pSFFrom->GetDisplayNameOf(apidl[i], SHGDN_FORPARSING, &strretFrom);
        if (FAILED(ret))
            goto cleanup;

        ret = StrRetToBufW(&strretFrom, NULL, pwszListPos, MAX_PATH);
        if (FAILED(ret))
            goto cleanup;

        pwszListPos += lstrlenW(pwszListPos) + 1;
    }
    /* Append the final null. */
    *pwszListPos = L'\0';

    /* Build a double null terminated target (this path) */
    ret = StringCchCopyW(wszDstPath, MAX_PATH, m_sPathTarget);
    if (FAILED(ret))
        goto cleanup;

    wszDstPath[lstrlenW(wszDstPath) + 1] = UNICODE_NULL;

    /* Set bRenameOnCollision to TRUE if necesssary */
    if (bCopy)
    {
        WCHAR szPath1[MAX_PATH], szPath2[MAX_PATH];
        GetFullPathNameW(pwszSrcPathsList, _countof(szPath1), szPath1, NULL);
        GetFullPathNameW(wszDstPath, _countof(szPath2), szPath2, NULL);
        PathRemoveFileSpecW(szPath1);
        if (_wcsicmp(szPath1, szPath2) == 0)
            bRenameOnCollision = TRUE;
    }

    ZeroMemory(&fop, sizeof(fop));
    fop.hwnd = m_hwndSite;
    fop.wFunc = bCopy ? FO_COPY : FO_MOVE;
    fop.pFrom = pwszSrcPathsList;
    fop.pTo = wszDstPath;
    fop.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR;
    if (bRenameOnCollision)
        fop.fFlags |= FOF_RENAMEONCOLLISION;

    ret = S_OK;

    if (SHFileOperationW(&fop))
    {
        ERR("SHFileOperationW failed\n");
        ret = E_FAIL;
    }

cleanup:
    HeapFree(GetProcessHeap(), 0, pwszSrcPathsList);
    return ret;
}

CFSDropTarget::CFSDropTarget():
    m_cfShellIDList(0),
    m_fAcceptFmt(FALSE),
    m_sPathTarget(NULL),
    m_hwndSite(NULL),
    m_grfKeyState(0)
{
}

HRESULT CFSDropTarget::Initialize(LPWSTR PathTarget)
{
    if (!PathTarget)
        return E_UNEXPECTED;

    m_cfShellIDList = RegisterClipboardFormatW(CFSTR_SHELLIDLIST);
    if (!m_cfShellIDList)
        return E_FAIL;

    m_sPathTarget = (WCHAR *)SHAlloc((wcslen(PathTarget) + 1) * sizeof(WCHAR));
    if (!m_sPathTarget)
        return E_OUTOFMEMORY;

    wcscpy(m_sPathTarget, PathTarget);

    return S_OK;
}

CFSDropTarget::~CFSDropTarget()
{
    SHFree(m_sPathTarget);
}

BOOL
CFSDropTarget::_GetUniqueFileName(LPCWSTR pwszBasePath, LPCWSTR pwszExt, LPWSTR pwszTarget, BOOL bShortcut)
{
    WCHAR wszLink[40];

    if (!bShortcut)
    {
        if (!LoadStringW(shell32_hInstance, IDS_LNK_FILE, wszLink, _countof(wszLink)))
            wszLink[0] = L'\0';
    }

    if (!bShortcut)
        swprintf(pwszTarget, L"%s%s%s", wszLink, pwszBasePath, pwszExt);
    else
        swprintf(pwszTarget, L"%s%s", pwszBasePath, pwszExt);

    for (UINT i = 2; PathFileExistsW(pwszTarget); ++i)
    {
        if (!bShortcut)
            swprintf(pwszTarget, L"%s%s (%u)%s", wszLink, pwszBasePath, i, pwszExt);
        else
            swprintf(pwszTarget, L"%s (%u)%s", pwszBasePath, i, pwszExt);
    }

    return TRUE;
}

/****************************************************************************
 * IDropTarget implementation
 */
BOOL CFSDropTarget::_QueryDrop(DWORD dwKeyState, LPDWORD pdwEffect)
{
    /* TODO Windows does different drop effects if dragging across drives.
    i.e., it will copy instead of move if the directories are on different disks. */

    DWORD dwEffect = m_dwDefaultEffect;

    *pdwEffect = DROPEFFECT_NONE;

    if (m_fAcceptFmt) { /* Does our interpretation of the keystate ... */
        *pdwEffect = KeyStateToDropEffect (dwKeyState);

        if (*pdwEffect == DROPEFFECT_NONE)
            *pdwEffect = dwEffect;

        /* ... matches the desired effect ? */
        if (dwEffect & *pdwEffect) {
            return TRUE;
        }
    }
    return FALSE;
}

HRESULT CFSDropTarget::_GetEffectFromMenu(IDataObject *pDataObject, POINTL pt, DWORD *pdwEffect, DWORD dwAvailableEffects)
{
    HMENU hmenu = LoadMenuW(shell32_hInstance, MAKEINTRESOURCEW(IDM_DRAGFILE));
    if (!hmenu)
        return E_OUTOFMEMORY;

    HMENU hpopupmenu = GetSubMenu(hmenu, 0);

    if ((dwAvailableEffects & DROPEFFECT_COPY) == 0)
        DeleteMenu(hpopupmenu, IDM_COPYHERE, MF_BYCOMMAND);
    else if ((dwAvailableEffects & DROPEFFECT_MOVE) == 0)
        DeleteMenu(hpopupmenu, IDM_MOVEHERE, MF_BYCOMMAND);
    else if ((dwAvailableEffects & DROPEFFECT_LINK) == 0)
        DeleteMenu(hpopupmenu, IDM_LINKHERE, MF_BYCOMMAND);

    if ((*pdwEffect & DROPEFFECT_COPY))
        SetMenuDefaultItem(hpopupmenu, IDM_COPYHERE, FALSE);
    else if ((*pdwEffect & DROPEFFECT_MOVE))
        SetMenuDefaultItem(hpopupmenu, IDM_MOVEHERE, FALSE);
    else if ((*pdwEffect & DROPEFFECT_LINK))
        SetMenuDefaultItem(hpopupmenu, IDM_LINKHERE, FALSE);

    /* FIXME: We need to support shell extensions here */

    /* We shouldn't use the site window here because the menu should work even when we don't have a site */
    HWND hwndDummy = CreateWindowEx(0,
                              WC_STATIC,
                              NULL,
                              WS_OVERLAPPED | WS_DISABLED | WS_CLIPSIBLINGS | WS_BORDER | SS_LEFT,
                              pt.x,
                              pt.y,
                              1,
                              1,
                              NULL,
                              NULL,
                              NULL,
                              NULL);

    UINT uCommand = TrackPopupMenu(hpopupmenu,
                                   TPM_LEFTALIGN | TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                   pt.x, pt.y, 0, hwndDummy, NULL);

    DestroyWindow(hwndDummy);

    if (uCommand == 0)
        return S_FALSE;
    else if (uCommand == IDM_COPYHERE)
        *pdwEffect = DROPEFFECT_COPY;
    else if (uCommand == IDM_MOVEHERE)
        *pdwEffect = DROPEFFECT_MOVE;
    else if (uCommand == IDM_LINKHERE)
        *pdwEffect = DROPEFFECT_LINK;

    return S_OK;
}

HRESULT CFSDropTarget::_RepositionItems(IShellFolderView *psfv, IDataObject *pdtobj, POINTL pt)
{
    CComPtr<IFolderView> pfv;
    POINT ptDrag;
    HRESULT hr = psfv->QueryInterface(IID_PPV_ARG(IFolderView, &pfv));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    hr = psfv->GetDragPoint(&ptDrag);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    PIDLIST_ABSOLUTE pidlFolder;
    PUITEMID_CHILD *apidl;
    UINT cidl;
    hr = SH_GetApidlFromDataObject(pdtobj, &pidlFolder, &apidl, &cidl);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    CComHeapPtr<POINT> apt;
    if (!apt.Allocate(cidl))
    {
        SHFree(pidlFolder);
        _ILFreeaPidl(apidl, cidl);
        return E_OUTOFMEMORY;
    }

    for (UINT i = 0; i<cidl; i++)
    {
        pfv->GetItemPosition(apidl[i], &apt[i]);
        apt[i].x += pt.x - ptDrag.x;
        apt[i].y += pt.y - ptDrag.y;
    }

    pfv->SelectAndPositionItems(cidl, apidl, apt, SVSI_SELECT);

    SHFree(pidlFolder);
    _ILFreeaPidl(apidl, cidl);
    return S_OK;
}

HRESULT WINAPI CFSDropTarget::DragEnter(IDataObject *pDataObject,
                                        DWORD dwKeyState, POINTL pt, DWORD *pdwEffect)
{
    TRACE("(%p)->(DataObject=%p)\n", this, pDataObject);

    if (*pdwEffect == DROPEFFECT_NONE)
        return S_OK;

    FORMATETC fmt;
    FORMATETC fmt2;
    m_fAcceptFmt = FALSE;

    InitFormatEtc (fmt, m_cfShellIDList, TYMED_HGLOBAL);
    InitFormatEtc (fmt2, CF_HDROP, TYMED_HGLOBAL);

    if (SUCCEEDED(pDataObject->QueryGetData(&fmt)))
        m_fAcceptFmt = TRUE;
    else if (SUCCEEDED(pDataObject->QueryGetData(&fmt2)))
        m_fAcceptFmt = TRUE;

    m_grfKeyState = dwKeyState;

#define D_NONE DROPEFFECT_NONE
#define D_COPY DROPEFFECT_COPY
#define D_MOVE DROPEFFECT_MOVE
#define D_LINK DROPEFFECT_LINK
    m_dwDefaultEffect = *pdwEffect;
    switch (*pdwEffect & (D_COPY | D_MOVE | D_LINK))
    {
        case D_COPY | D_MOVE:
            if (dwKeyState & MK_CONTROL)
                m_dwDefaultEffect = D_COPY;
            else
                m_dwDefaultEffect = D_MOVE;
            break;
        case D_COPY | D_MOVE | D_LINK:
            if ((dwKeyState & (MK_SHIFT | MK_CONTROL)) == (MK_SHIFT | MK_CONTROL))
                m_dwDefaultEffect = D_LINK;
            else if ((dwKeyState & (MK_SHIFT | MK_CONTROL)) == MK_CONTROL)
                m_dwDefaultEffect = D_COPY;
            else
                m_dwDefaultEffect = D_MOVE;
            break;
        case D_COPY | D_LINK:
            if ((dwKeyState & (MK_SHIFT | MK_CONTROL)) == (MK_SHIFT | MK_CONTROL))
                m_dwDefaultEffect = D_LINK;
            else
                m_dwDefaultEffect = D_COPY;
            break;
        case D_MOVE | D_LINK:
            if ((dwKeyState & (MK_SHIFT | MK_CONTROL)) == (MK_SHIFT | MK_CONTROL))
                m_dwDefaultEffect = D_LINK;
            else
                m_dwDefaultEffect = D_MOVE;
            break;
    }

    STGMEDIUM medium;
    if (SUCCEEDED(pDataObject->GetData(&fmt2, &medium)))
    {
        WCHAR wstrFirstFile[MAX_PATH];
        if (DragQueryFileW((HDROP)medium.hGlobal, 0, wstrFirstFile, _countof(wstrFirstFile)))
        {
            /* Check if the drive letter is different */
            if (wstrFirstFile[0] != m_sPathTarget[0])
            {
                m_dwDefaultEffect = DROPEFFECT_COPY;
            }
        }
        ReleaseStgMedium(&medium);
    }

    if (!m_fAcceptFmt)
        *pdwEffect = DROPEFFECT_NONE;
    else
        *pdwEffect = m_dwDefaultEffect;

    return S_OK;
}

HRESULT WINAPI CFSDropTarget::DragOver(DWORD dwKeyState, POINTL pt,
                                       DWORD *pdwEffect)
{
    TRACE("(%p)\n", this);

    if (!pdwEffect)
        return E_INVALIDARG;

    m_grfKeyState = dwKeyState;

    _QueryDrop(dwKeyState, pdwEffect);

    return S_OK;
}

HRESULT WINAPI CFSDropTarget::DragLeave()
{
    TRACE("(%p)\n", this);

    m_fAcceptFmt = FALSE;

    return S_OK;
}

HRESULT WINAPI CFSDropTarget::Drop(IDataObject *pDataObject,
                                   DWORD dwKeyState, POINTL pt, DWORD *pdwEffect)
{
    TRACE("(%p) object dropped, effect %u\n", this, *pdwEffect);

    if (!pdwEffect)
        return E_INVALIDARG;

    IUnknown_GetWindow(m_site, &m_hwndSite);

    DWORD dwAvailableEffects = *pdwEffect;

    _QueryDrop(dwKeyState, pdwEffect);

    TRACE("pdwEffect: 0x%x, m_dwDefaultEffect: 0x%x, dwAvailableEffects: 0x%x\n", *pdwEffect, m_dwDefaultEffect, dwAvailableEffects);

    if (m_grfKeyState & MK_RBUTTON)
    {
        HRESULT hr = _GetEffectFromMenu(pDataObject, pt, pdwEffect, dwAvailableEffects);
        if (FAILED_UNEXPECTEDLY(hr) || hr == S_FALSE)
            return hr;
    }

    if (*pdwEffect == DROPEFFECT_MOVE && m_site)
    {
        CComPtr<IShellFolderView> psfv;
        HRESULT hr = IUnknown_QueryService(m_site, SID_IFolderView, IID_PPV_ARG(IShellFolderView, &psfv));
        if (SUCCEEDED(hr) && psfv->IsDropOnSource(this) == S_OK)
        {
            _RepositionItems(psfv, pDataObject, pt);
            return S_OK;
        }
    }

    BOOL fIsOpAsync = FALSE;
    CComPtr<IAsyncOperation> pAsyncOperation;

    if (SUCCEEDED(pDataObject->QueryInterface(IID_PPV_ARG(IAsyncOperation, &pAsyncOperation))))
    {
        if (SUCCEEDED(pAsyncOperation->GetAsyncMode(&fIsOpAsync)) && fIsOpAsync)
        {
            _DoDropData *data = static_cast<_DoDropData*>(HeapAlloc(GetProcessHeap(), 0, sizeof(_DoDropData)));
            data->This = this;
            // Need to maintain this class in case the window is closed or the class exists temporarily (when dropping onto a folder).
            pDataObject->AddRef();
            pAsyncOperation->StartOperation(NULL);
            CoMarshalInterThreadInterfaceInStream(IID_IDataObject, pDataObject, &data->pStream);
            this->AddRef();
            data->dwKeyState = dwKeyState;
            data->pt = pt;
            // Need to dereference as pdweffect gets freed.
            data->pdwEffect = *pdwEffect;
            SHCreateThread(CFSDropTarget::_DoDropThreadProc, data, NULL, NULL);
            return S_OK;
        }
    }
    return this->_DoDrop(pDataObject, dwKeyState, pt, pdwEffect);
}

HRESULT
WINAPI
CFSDropTarget::SetSite(IUnknown *pUnkSite)
{
    m_site = pUnkSite;
    return S_OK;
}

HRESULT
WINAPI
CFSDropTarget::GetSite(REFIID riid, void **ppvSite)
{
    if (!m_site)
        return E_FAIL;

    return m_site->QueryInterface(riid, ppvSite);
}

HRESULT CFSDropTarget::_DoDrop(IDataObject *pDataObject,
                               DWORD dwKeyState, POINTL pt, DWORD *pdwEffect)
{
    TRACE("(%p) performing drop, effect %u\n", this, *pdwEffect);
    FORMATETC fmt;
    FORMATETC fmt2;
    STGMEDIUM medium;

    InitFormatEtc (fmt, m_cfShellIDList, TYMED_HGLOBAL);
    InitFormatEtc (fmt2, CF_HDROP, TYMED_HGLOBAL);

    HRESULT hr;
    bool bCopy = TRUE;
    bool bLinking = FALSE;

    /* Figure out what drop operation we're doing */
    if (pdwEffect)
    {
        TRACE("Current drop effect flag %i\n", *pdwEffect);
        if ((*pdwEffect & DROPEFFECT_MOVE) == DROPEFFECT_MOVE)
            bCopy = FALSE;
        if ((*pdwEffect & DROPEFFECT_LINK) == DROPEFFECT_LINK)
            bLinking = TRUE;
    }

    if (SUCCEEDED(pDataObject->QueryGetData(&fmt)))
    {
        hr = pDataObject->GetData(&fmt, &medium);
        TRACE("CFSTR_SHELLIDLIST.\n");
        if (FAILED(hr))
        {
            ERR("CFSTR_SHELLIDLIST failed\n");
        }
        /* lock the handle */
        LPIDA lpcida = (LPIDA)GlobalLock(medium.hGlobal);
        if (!lpcida)
        {
            ReleaseStgMedium(&medium);
            return E_FAIL;
        }

        /* convert the data into pidl */
        LPITEMIDLIST pidl;
        LPITEMIDLIST *apidl = _ILCopyCidaToaPidl(&pidl, lpcida);
        if (!apidl)
        {
            ReleaseStgMedium(&medium);
            return E_FAIL;
        }

        CComPtr<IShellFolder> psfDesktop;
        CComPtr<IShellFolder> psfFrom = NULL;

        /* Grab the desktop shell folder */
        hr = SHGetDesktopFolder(&psfDesktop);
        if (FAILED(hr))
        {
            ERR("SHGetDesktopFolder failed\n");
            SHFree(pidl);
            _ILFreeaPidl(apidl, lpcida->cidl);
            ReleaseStgMedium(&medium);
            return E_FAIL;
        }

        /* Find source folder, this is where the clipboard data was copied from */
        if (_ILIsDesktop(pidl))
        {
            /* use desktop shell folder */
            psfFrom = psfDesktop;
        }
        else
        {
            hr = psfDesktop->BindToObject(pidl, NULL, IID_PPV_ARG(IShellFolder, &psfFrom));
            if (FAILED(hr))
            {
                ERR("no IShellFolder\n");
                SHFree(pidl);
                _ILFreeaPidl(apidl, lpcida->cidl);
                ReleaseStgMedium(&medium);
                return E_FAIL;
            }
        }

        if (bLinking)
        {
            WCHAR wszPath[MAX_PATH];
            WCHAR wszTarget[MAX_PATH];

            TRACE("target path = %s\n", debugstr_w(m_sPathTarget));

            /* We need to create a link for each pidl in the copied items, so step through the pidls from the clipboard */
            BOOL fCreatePidlLinks = SHELL_Experiment(SHELL_DRAGTOLINK_PIDLTARGET);
            HWND hWnd = IsWindowVisible(m_hwndSite) ? m_hwndSite : NULL;
            for (UINT i = 0; i < lpcida->cidl; i++)
            {
                if (fCreatePidlLinks)
                {
                    DbgPrint("X: SHELL_DRAGTOLINK_PIDLTARGET\n");
                    CComHeapPtr<ITEMIDLIST> absolute;
                    hr = SHILCombine(pidl, apidl[i], &absolute);
                    if (FAILED_UNEXPECTEDLY(hr))
                        break;
                    hr = SHELL32_CreateLink(hWnd, m_sPathTarget, absolute);
                    if (FAILED_UNEXPECTEDLY(hr))
                        break;
                    continue;
                }

                // Find out which file we're linking.
                STRRET strFile;
                hr = psfFrom->GetDisplayNameOf(apidl[i], SHGDN_FORPARSING, &strFile);
                if (FAILED_UNEXPECTEDLY(hr))
                    break;

                hr = StrRetToBufW(&strFile, apidl[i], wszPath, _countof(wszPath));
                if (FAILED_UNEXPECTEDLY(hr))
                    break;

                TRACE("source path = %s\n", debugstr_w(wszPath));

                WCHAR wszDisplayName[MAX_PATH];
                LPWSTR pwszFileName = PathFindFileNameW(wszPath);
                if (PathIsRootW(wszPath)) // Drive?
                {
                    hr = psfFrom->GetDisplayNameOf(apidl[i], SHGDN_NORMAL, &strFile);
                    if (FAILED_UNEXPECTEDLY(hr))
                        break;

                    hr = StrRetToBufW(&strFile, apidl[i], wszDisplayName, _countof(wszDisplayName));
                    if (FAILED_UNEXPECTEDLY(hr))
                        break;

                    // Delete a ':' in wszDisplayName.
                    LPWSTR pch0 = wcschr(wszDisplayName, L':');
                    if (pch0)
                    {
                        do
                        {
                            *pch0 = *(pch0 + 1);
                            ++pch0;
                        } while (*pch0);
                    }

                    pwszFileName = wszDisplayName; // Use wszDisplayName
                }
                else if (wszPath[0] == L':' && wszPath[1] == L':') // ::{GUID}?
                {
                    CLSID clsid;
                    hr = ::CLSIDFromString(&wszPath[2], &clsid);
                    if (SUCCEEDED(hr))
                    {
                        LPITEMIDLIST pidl = ILCreateFromPathW(wszPath);
                        if (pidl)
                        {
                            SHFILEINFOW fi = { NULL };
                            SHGetFileInfoW((LPCWSTR)pidl, 0, &fi, sizeof(fi),
                                           SHGFI_DISPLAYNAME | SHGFI_PIDL);
                            if (fi.szDisplayName[0])
                            {
                                lstrcpynW(wszDisplayName, fi.szDisplayName, _countof(wszDisplayName));
                                pwszFileName = wszDisplayName; // Use wszDisplayName
                            }
                            ILFree(pidl);
                        }
                    }
                }

                // Creating a buffer to hold the combined path.
                WCHAR wszCombined[MAX_PATH];
                PathCombineW(wszCombined, m_sPathTarget, pwszFileName);

                // Check to see if the source is a link
                BOOL fSourceIsLink = FALSE;
                if (!wcsicmp(PathFindExtensionW(wszPath), L".lnk"))
                {
                    fSourceIsLink = TRUE;
                    PathRemoveExtensionW(wszCombined);
                }

                // Create a pathname to save the new link.
                _GetUniqueFileName(wszCombined, L".lnk", wszTarget, TRUE);

                CComPtr<IPersistFile> ppf;
                if (fSourceIsLink)
                {
                    hr = IShellLink_ConstructFromPath(wszPath, IID_PPV_ARG(IPersistFile, &ppf));
                    if (FAILED_UNEXPECTEDLY(hr))
                        break;
                }
                else
                {
                    CComPtr<IShellLinkW> pLink;
                    hr = CShellLink::_CreatorClass::CreateInstance(NULL, IID_PPV_ARG(IShellLinkW, &pLink));
                    if (FAILED_UNEXPECTEDLY(hr))
                        break;

                    WCHAR szDirPath[MAX_PATH], *pwszFile;
                    GetFullPathName(wszPath, MAX_PATH, szDirPath, &pwszFile);
                    if (pwszFile)
                        pwszFile[0] = 0;

                    hr = pLink->SetPath(wszPath);
                    if (FAILED_UNEXPECTEDLY(hr))
                        break;

                    hr = pLink->SetWorkingDirectory(szDirPath);
                    if (FAILED_UNEXPECTEDLY(hr))
                        break;

                    hr = pLink->QueryInterface(IID_PPV_ARG(IPersistFile, &ppf));
                    if (FAILED_UNEXPECTEDLY(hr))
                        break;
                }

                hr = ppf->Save(wszTarget, !fSourceIsLink);
                if (FAILED_UNEXPECTEDLY(hr))
                    break;

                SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, wszTarget, NULL);
            }
        }
        else
        {
            hr = _CopyItems(psfFrom, lpcida->cidl, (LPCITEMIDLIST*)apidl, bCopy);
        }

        SHFree(pidl);
        _ILFreeaPidl(apidl, lpcida->cidl);
        ReleaseStgMedium(&medium);
    }
    else if (SUCCEEDED(pDataObject->QueryGetData(&fmt2)))
    {
        FORMATETC fmt2;
        InitFormatEtc (fmt2, CF_HDROP, TYMED_HGLOBAL);
        if (SUCCEEDED(pDataObject->GetData(&fmt2, &medium)) /* && SUCCEEDED(pDataObject->GetData(&fmt2, &medium))*/)
        {
            WCHAR wszTargetPath[MAX_PATH + 1];
            LPWSTR pszSrcList;

            wcscpy(wszTargetPath, m_sPathTarget);
            //Double NULL terminate.
            wszTargetPath[wcslen(wszTargetPath) + 1] = '\0';

            LPDROPFILES lpdf = (LPDROPFILES) GlobalLock(medium.hGlobal);
            if (!lpdf)
            {
                ERR("Error locking global\n");
                return E_FAIL;
            }
            pszSrcList = (LPWSTR) (((byte*) lpdf) + lpdf->pFiles);
            ERR("Source file (just the first) = %s, target path = %s, bCopy: %d\n", debugstr_w(pszSrcList), debugstr_w(wszTargetPath), bCopy);

            SHFILEOPSTRUCTW op;
            ZeroMemory(&op, sizeof(op));
            op.pFrom = pszSrcList;
            op.pTo = wszTargetPath;
            op.hwnd = m_hwndSite;
            op.wFunc = bCopy ? FO_COPY : FO_MOVE;
            op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR;
            int res = SHFileOperationW(&op);
            if (res)
            {
                ERR("SHFileOperationW failed with 0x%x\n", res);
                hr = E_FAIL;
            }

            return hr;
        }
        ERR("Error calling GetData\n");
        hr = E_FAIL;
    }
    else
    {
        ERR("No viable drop format.\n");
        hr = E_FAIL;
    }
    return hr;
}

DWORD WINAPI CFSDropTarget::_DoDropThreadProc(LPVOID lpParameter)
{
    CoInitialize(NULL);
    _DoDropData *data = static_cast<_DoDropData*>(lpParameter);
    CComPtr<IDataObject> pDataObject;
    HRESULT hr = CoGetInterfaceAndReleaseStream (data->pStream, IID_PPV_ARG(IDataObject, &pDataObject));

    if (SUCCEEDED(hr))
    {
        CComPtr<IAsyncOperation> pAsyncOperation;
        hr = data->This->_DoDrop(pDataObject, data->dwKeyState, data->pt, &data->pdwEffect);
        if (SUCCEEDED(pDataObject->QueryInterface(IID_PPV_ARG(IAsyncOperation, &pAsyncOperation))))
        {
            pAsyncOperation->EndOperation(hr, NULL, data->pdwEffect);
        }
    }
    //Release the CFSFolder and data object holds in the copying thread.
    data->This->Release();
    //Release the parameter from the heap.
    HeapFree(GetProcessHeap(), 0, data);
    CoUninitialize();
    return 0;
}

HRESULT CFSDropTarget_CreateInstance(LPWSTR sPathTarget, REFIID riid, LPVOID * ppvOut)
{
    return ShellObjectCreatorInit<CFSDropTarget>(sPathTarget, riid, ppvOut);
}
