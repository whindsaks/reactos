/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Test for DefFolderMenu/SHCreateDefaultContextMenu
 * COPYRIGHT:   Copyright 2024 Whindmar Saksit <whindsaks@proton.me>
 */

#include "shelltest.h"
#include <shlwapi_undoc.h>
#include <shellutils.h>

#define CDFMC2 CDefFolderMenu_Create2
typedef HRESULT (WINAPI*SHCREATEDEFAULTCONTEXTMENU)(const DEFCONTEXTMENU*, REFIID, void**);
SHCREATEDEFAULTCONTEXTMENU g_SHCDCM;

enum {
    IDC_MIDDLE = 10, // Unusual value on purpose
    IDC_BOTTOM,
    IDC_TOP,
    QCM_FIRST = 1000, // Unusual value on purpose
    QCM_LAST = 0x7fff, // The maximum value that can be used without clashing with LOWORD(DFM_CMD_*)
    CMF_NOBOTTOM = 1 << 16, // Custom CMF value
    CMF_BOTTOMATTOP = 1 << 17,
};
C_ASSERT(CMF_NOBOTTOM & CMF_RESERVED);

static HDPA g_hMsgStats; // PR #7129 decided not to use the STL in shell32 tests, using this instead
static IShellFolder *g_pNormalFolder;
static PCUITEMID_CHILD g_pFileLastId;

static UINT GetWinMajor() { return LOBYTE(GetVersion()); }
#define Stat_Reset() DPA_SetPtrCount(g_hMsgStats, 0)
#define Begin(hr) ( Stat_Reset(), (hr) )

static BOOL CALLBACK GetStatMessageIndexCallback(void *ptr, void *ctx)
{
    UINT *data = (UINT*)ctx;
    if (LOWORD((SIZE_T)ptr) != data[0])
        return TRUE;
    data[1] = HIWORD((SIZE_T)ptr);
    return FALSE;
}

static int Stat_GetMessageIndex(WORD msg)
{
    UINT data[2] = { msg, (UINT)-1 };
    if (g_hMsgStats)
        DPA_EnumCallback(g_hMsgStats, GetStatMessageIndexCallback, data);
    return data[1];
}

static bool Stat_GotMessage(WORD msg)
{
    return Stat_GetMessageIndex(msg) != -1;
}

static void Stat_AddMessage(WORD msg)
{
    if (!g_hMsgStats || Stat_GotMessage(msg))
        return;
    DPA_AppendPtr(g_hMsgStats, (void*)MAKELONG(msg, DPA_GetPtrCount(g_hMsgStats)));
}

#define DFMINSERT(qcmi, ID) DfmInsert((qcmi), (ID), #ID)
static HRESULT DfmInsert(QCMINFO &qcmi, WORD CmdId, LPCSTR Text)
{
    UINT idAbsolute = qcmi.idCmdFirst + CmdId;
    if (idAbsolute > qcmi.idCmdLast)
        return HRESULT_FROM_WIN32(ERROR_INVALID_INDEX);
    if (!InsertMenuA(qcmi.hmenu, qcmi.indexMenu, MF_BYPOSITION, idAbsolute, Text))
        return E_FAIL;
    qcmi.idCmdFirst = idAbsolute + 1;
    return S_OK;
}

static HRESULT SHELL32_DefaultContextMenuCallBack(UINT msg)
{
    switch (msg)
    {
        case DFM_MERGECONTEXTMENU:
            return S_OK; // Yes, I want verbs
        case DFM_INVOKECOMMAND:
            return S_FALSE; // Do it for me please
        case DFM_GETDEFSTATICID:
            return S_FALSE; // Supposedly "required for Windows 7 to pick a default"
    }
    return E_NOTIMPL;
}

static HRESULT GetVerb(IContextMenu *pCM, SIZE_T Id, LPSTR Buf, UINT cchBuf, UINT *pRes = NULL)
{
    HRESULT hr = pCM->GetCommandString(Id, GCS_VERBA, pRes, Buf, cchBuf);
    if (FAILED(hr))
    {
        WCHAR VerbW[MAX_PATH] = L"?";
        hr = pCM->GetCommandString(Id, GCS_VERBW, pRes, (LPSTR)VerbW, _countof(VerbW));
        hr = SUCCEEDED(hr) ? (SHUnicodeToAnsi(VerbW, Buf, cchBuf) ? S_OK : E_FAIL) : hr;
    }
    return hr;
}

static BOOL IsVerb(IContextMenu *pCM, SIZE_T Id, LPCSTR VerbA)
{
    char buf[MAX_PATH] = "?";
    return SUCCEEDED(GetVerb(pCM, Id, buf, _countof(buf))) && !lstrcmpiA(buf, VerbA);
}

static UINT HasVerbEx(IContextMenu *pCM, LPCSTR VerbA)
{
    for (UINT i = 0; i < (QCM_LAST - QCM_FIRST) && pCM; ++i)
    {
        if (IsVerb(pCM, i, VerbA))
            return TRUE | (i << 1);
    }
    return FALSE;
}

static bool HasVerb(IContextMenu *pCM, LPCSTR VerbA)
{
    return HasVerbEx(pCM, VerbA) != FALSE;
}

static BOOL IsVerbByPos(IContextMenu *pCM, LPCSTR VerbA, HMENU hMenu, UINT Pos)
{
    int WinId = GetMenuItemID(hMenu, Pos), CmdId = WinId - QCM_FIRST;
    return WinId != -1 && HasVerbEx(pCM, VerbA) == (TRUE | ((UINT)CmdId << 1));
}

static HRESULT InvokeCommand(IContextMenu *pCM, LPCSTR VerbA, UINT Flags = CMIC_MASK_NOASYNC)
{
    CMINVOKECOMMANDINFO ici = { sizeof(ici), Flags, NULL, VerbA, NULL, NULL, SW_SHOW };
    return pCM->InvokeCommand(&ici);
}

static HRESULT QueryContextMenu(IContextMenu *pCM, BOOL GetCount = FALSE, UINT CMF = 0, HMENU *phMenu = NULL)
{
    HMENU hMenu = CreatePopupMenu();
    HRESULT hr = pCM->QueryContextMenu(hMenu, 0, QCM_FIRST, QCM_LAST, CMF);
    if (SUCCEEDED(hr) && GetCount)
        hr = GetMenuItemCount(hMenu);
    if (phMenu)
        *phMenu = hMenu;
    else
        DestroyMenu(hMenu);
    return hr;
}

static void BeginContextMenuCallBack(UINT uMsg, LPARAM lParam)
{
    Stat_AddMessage(uMsg);
}

#define CDFMC2TEST_DECLARE(name) { struct name : CDFMC2TEST {
#define CDFMC2TEST_RUN(flags) } testobj; ok_int(CDFMC2TestStarter(testobj, (flags)), S_OK); }
#define TF_SF               0x01
#define TF_APIDL            0x02
#define TF_HKEYS            0x04 // TODO: SHGetAssocKeys unavailable and ASSOCKEY_CLASS not implemented in ROS
#define TF_NORMALFILE (TF_SF | TF_APIDL | TF_HKEYS)
static void* g_pCDFMC2TEST;
struct CDFMC2TEST // CDFMC2TEST_DECLARE inherits from this base class
{
    IContextMenu *pCM;
    HRESULT hrCreate;

    CDFMC2TEST() { g_pCDFMC2TEST = this; Constructor(); }
    virtual void Constructor() {}
    HRESULT DefaultHandler(UINT uMsg) { return SHELL32_DefaultContextMenuCallBack(uMsg); }
    virtual void OnCreated() { ok(SUCCEEDED(QueryContextMenu(pCM, FALSE)), "Basic QCM\n"); }

    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar)
    {
        return DefaultHandler(uMsg);
    }
    static HRESULT CALLBACK AbiCallback(IShellFolder*, HWND, IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar)
    {
        BeginContextMenuCallBack(uMsg, lPar);
        return ((CDFMC2TEST*)g_pCDFMC2TEST)->Callback(pDO, uMsg, wPar, lPar);
    }
};

static HRESULT CDFMC2TestStarter(CDFMC2TEST &Test, UINT Flags)
{
    IShellFolder *pSF = NULL;
    PCUITEMID_CHILD pidlFileChild = NULL, *apidl;
    HKEY *phKeys = NULL;
    UINT cidl = 0, cKeys = 0;

    if ((Flags & TF_SF) && (pSF = g_pNormalFolder) == NULL)
    {
        skip("Unable to initialize test\n");
        return HRESULT_FROM_WIN32(ERROR_INTERNAL_ERROR);
    }
    if ((Flags & TF_APIDL) && (cidl = 1) != 0 && ILIsEmpty(pidlFileChild = g_pFileLastId))
    {
        skip("Unable to initialize test\n");
        return HRESULT_FROM_WIN32(ERROR_INTERNAL_ERROR);
    }
    CComPtr<IContextMenu> pCM;
    apidl = pidlFileChild ? &pidlFileChild : NULL;
    HRESULT hr = Begin(CDFMC2(NULL, NULL, cidl, apidl, pSF, Test.AbiCallback, cKeys, phKeys, &pCM));
    Test.pCM = pCM;
    Test.hrCreate = hr;
    if (SUCCEEDED(hr))
        Test.OnCreated();
    return hr;
}

#define HasFileCommands() ( HasVerb(pCM, "open") || HasVerb(pCM, "openas") || HasVerb(pCM, "runas") )
#define HasFolderCommands() ( HasVerb(pCM, "link") || HasVerb(pCM, "properties") )

static HRESULT CALLBACK MergePosCB(IShellFolder *, HWND, IDataObject *, UINT uMsg, WPARAM wPar, LPARAM lPar)
{
    BeginContextMenuCallBack(uMsg, lPar);
    QCMINFO *pQCMI = (QCMINFO*)lPar;
    if (uMsg == DFM_MERGECONTEXTMENU_TOP)
    {
        return DFMINSERT(*pQCMI, IDC_TOP);
    }
    if (uMsg == DFM_MERGECONTEXTMENU)
    {
        DFMINSERT(*pQCMI, IDC_MIDDLE);
        return S_OK;
    }
    if (uMsg == DFM_MERGECONTEXTMENU_BOTTOM && !(wPar & CMF_NOBOTTOM))
    {
        UINT originalIndex = pQCMI->indexMenu;
        if (wPar & CMF_BOTTOMATTOP)
            pQCMI->indexMenu = 0;
        HRESULT hr = DFMINSERT(*pQCMI, IDC_BOTTOM);
        pQCMI->indexMenu = originalIndex;
        return hr;
    }
    return SHELL32_DefaultContextMenuCallBack(uMsg);
}

void BasicTests(IShellFolder *pSF, PCUITEMID_CHILD pidlChild)
{
    HKEY hKey;
    HRESULT hr;


    //hr = Begin(CDFMC2(NULL, NULL, 0, NULL, NULL, NULL, 0, NULL, NULL)); // Crashes on Windows
    //ok_int(FAILED(hr), TRUE);


    if (!RegOpenKeyExA(HKEY_CURRENT_USER, "Software", 0, KEY_READ, &hKey))
    {
        CComPtr<IContextMenu> pCM;
        hr = Begin(CDFMC2(NULL, NULL, 1, &pidlChild, pSF, MergePosCB, 1, &hKey, &pCM));
        if (SUCCEEDED(hr))
        {
            ok_int(pCM.Detach()->Release(), 0);
            ok(RegCloseKey(hKey) == ERROR_SUCCESS, "Must duplicate the HKEYs\n");
        }
        else
        {
            RegCloseKey(hKey);
            skip("Unable to initialize test\n");
        }
    }


    CDFMC2TEST_DECLARE(ContextMenuImplementsObjectWithSite)
    virtual void OnCreated() override
    {
        CComPtr<IObjectWithSite> SiteObj;
        ok_hr(pCM->QueryInterface(IID_PPV_ARG(IObjectWithSite, &SiteObj)), S_OK);
    }
    CDFMC2TEST_RUN(0)


    CDFMC2TEST_DECLARE(FirstCallbackMessage)
    virtual void OnCreated() override
    {
        ok_int(Stat_GetMessageIndex(/*DFM_?*/ 3), 0); // This unknown message is sent first
    }
    CDFMC2TEST_RUN(0)


    CDFMC2TEST_DECLARE(FolderCommandsBeforeMCM)
    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar) override
    {
        if (uMsg == DFM_MERGECONTEXTMENU)
            ok(HasFolderCommands(), "Folder commands must be added before MCM\n");
        return DefaultHandler(uMsg);
    }
    virtual void OnCreated() override
    {
        QueryContextMenu(pCM, TRUE, CMF_NORMAL);
    }
    CDFMC2TEST_RUN(TF_NORMALFILE)


    CDFMC2TEST_DECLARE(NoFolderCommandsWithVerbsOnly)
    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar) override
    {
        if (uMsg == DFM_MERGECONTEXTMENU)
            ok(!HasFolderCommands(), "Folder commands must not be added with CMF_VERBSONLY\n");
        return DefaultHandler(uMsg);
    }
    virtual void OnCreated() override
    {
        QueryContextMenu(pCM, TRUE, CMF_VERBSONLY);
    }
    CDFMC2TEST_RUN(TF_NORMALFILE)


    CDFMC2TEST_DECLARE(GetDefStaticIdSFalseMCM)
    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar) override
    {
        if (uMsg == DFM_MERGECONTEXTMENU)
            return S_FALSE;
        return DefaultHandler(uMsg);
    }
    virtual void OnCreated() override
    {
        ok_int(Stat_GotMessage(DFM_GETDEFSTATICID), FALSE);
        ok_int(QueryContextMenu(pCM, TRUE, CMF_VERBSONLY | CMF_NOVERBS | CMF_NODEFAULT), 0);
        ok_int(Stat_GotMessage(DFM_GETDEFSTATICID), FALSE);
        ok_int(QueryContextMenu(pCM, TRUE, CMF_VERBSONLY | CMF_NOVERBS), 0);
        ok_int(Stat_GotMessage(DFM_GETDEFSTATICID), TRUE);
    }
    CDFMC2TEST_RUN(TF_NORMALFILE)


    CDFMC2TEST_DECLARE(GetDefStaticIdDefaultMCM)
    virtual void OnCreated() override
    {
        ok_int(Stat_GotMessage(DFM_GETDEFSTATICID), FALSE);
        ok_int(QueryContextMenu(pCM, TRUE, CMF_NOVERBS) > 0, TRUE); // CMF_NOVERBS because we don't want "open" to be set as the default
        ok_int(Stat_GotMessage(DFM_GETDEFSTATICID), TRUE);
        ok_int(Stat_GetMessageIndex(DFM_GETDEFSTATICID) > Stat_GetMessageIndex(DFM_MERGECONTEXTMENU), TRUE);
        ok_int(Stat_GetMessageIndex(DFM_GETDEFSTATICID) > Stat_GetMessageIndex(DFM_MERGECONTEXTMENU_BOTTOM), TRUE);
        ok_int(Stat_GetMessageIndex(DFM_GETDEFSTATICID) < Stat_GetMessageIndex(DFM_MERGECONTEXTMENU_TOP), TRUE);
    }
    CDFMC2TEST_RUN(TF_NORMALFILE)


    CDFMC2TEST_DECLARE(SFalseMCMDoesNotAddVerbs)
    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar) override
    {
        if (uMsg == DFM_MERGECONTEXTMENU)
            return S_FALSE;
        return DefaultHandler(uMsg);
    }
    virtual void OnCreated() override
    {
        QueryContextMenu(pCM, TRUE);
        ok_int(Stat_GotMessage(DFM_GETDEFSTATICID), TRUE);
        ok(!HasFileCommands(), "S_FALSE MCM must act like CMF_NOVERBS\n");
        ok(HasFolderCommands(), "Must have folder commands\n");
    }
    CDFMC2TEST_RUN(TF_NORMALFILE)


    CDFMC2TEST_DECLARE(OnlyShellFolderParam)
    virtual void OnCreated() override
    {
        ok(QueryContextMenu(pCM, TRUE) == 0, "Menu should be empty\n");
        ok_int(Stat_GotMessage(DFM_MERGECONTEXTMENU), TRUE);
    }
    CDFMC2TEST_RUN(TF_SF)


    CDFMC2TEST_DECLARE(MiddleAndBottomSharedIdRange)
    UINT FirstMCMM, FirstMCMB;
    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar) override
    {
        if (uMsg == DFM_MERGECONTEXTMENU)
            FirstMCMM = ((QCMINFO*)lPar)->idCmdFirst;
        if (uMsg == DFM_MERGECONTEXTMENU_BOTTOM)
            FirstMCMB = ((QCMINFO*)lPar)->idCmdFirst;
        return DefaultHandler(uMsg);
    }
    virtual void OnCreated() override
    {
        FirstMCMB = 0;
        QueryContextMenu(pCM, TRUE);
        // Note: This is only true on NT5 if no DFM_MERGECONTEXTMENU items are added. Always true on NT6.
        ok(FirstMCMM == FirstMCMB, "\"MIDDLE\" and BOTTOM share the same id range\n");
    }
    CDFMC2TEST_RUN(TF_NORMALFILE)


    CDFMC2TEST_DECLARE(NullParamsMergeOrder)
    virtual void OnCreated() override
    {
        ok_int(Stat_GotMessage(DFM_MERGECONTEXTMENU), FALSE);
        ok(QueryContextMenu(pCM, TRUE) == 0, "Menu should be empty\n");
        ok_int(Stat_GotMessage(DFM_MERGECONTEXTMENU), TRUE);
        ok_int(Stat_GotMessage(DFM_MERGECONTEXTMENU_BOTTOM), TRUE);
        ok_int(Stat_GetMessageIndex(DFM_MERGECONTEXTMENU_BOTTOM) > Stat_GetMessageIndex(DFM_MERGECONTEXTMENU), TRUE);
        ok_int(Stat_GotMessage(DFM_MERGECONTEXTMENU_TOP), TRUE);
        ok_int(Stat_GetMessageIndex(DFM_MERGECONTEXTMENU_TOP) > Stat_GetMessageIndex(DFM_MERGECONTEXTMENU_BOTTOM), TRUE);

        ok_int(Stat_GotMessage(DFM_MAPCOMMANDNAME), FALSE); // We have not executed anything yet
        ok(FAILED(InvokeCommand(pCM, "ThisVerbDoesNotExist")), "Not a real verb\n");
        ok_int(Stat_GotMessage(DFM_MAPCOMMANDNAME), TRUE);
    }
    CDFMC2TEST_RUN(0)


    CDFMC2TEST_DECLARE(NullParamsMergePos)
    UINT FirstMCMT, FirstMCMM, FirstMCMB;
    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar) override
    {
        if (uMsg == DFM_MERGECONTEXTMENU_TOP)
            FirstMCMT = ((QCMINFO*)lPar)->idCmdFirst;
        if (uMsg == DFM_MERGECONTEXTMENU)
            FirstMCMM = ((QCMINFO*)lPar)->idCmdFirst;
        if (uMsg == DFM_MERGECONTEXTMENU_BOTTOM)
            FirstMCMB = ((QCMINFO*)lPar)->idCmdFirst;
        return MergePosCB(NULL, NULL, pDO, uMsg, wPar, lPar);
    }
    virtual void OnCreated() override
    {
        HMENU hMenu;
        ok_int(QueryContextMenu(pCM, TRUE, CMF_VERBSONLY | CMF_NOVERBS, &hMenu), 3 + 1); // + 1  for separator
        ok_int(GetMenuPosFromID(hMenu, FirstMCMT + IDC_TOP), 0);
        ok_int(GetMenuPosFromID(hMenu, FirstMCMM + IDC_MIDDLE), 1);
        ok_int(GetMenuPosFromID(hMenu, FirstMCMB + IDC_BOTTOM), 2 + 1);
        DestroyMenu(hMenu);
        ok_int(QueryContextMenu(pCM, TRUE, CMF_VERBSONLY | CMF_NOVERBS | CMF_NOBOTTOM), 2); // TOP + "MIDDLE"
        ok_int(QueryContextMenu(pCM, TRUE, CMF_VERBSONLY | CMF_NOVERBS | CMF_BOTTOMATTOP), 3); // Placing BOTTOM at the top should remove the separator
    }
    CDFMC2TEST_RUN(0)


    CDFMC2TEST_DECLARE(FileMergePos)
    UINT FirstMCMT, FirstMCMM, FirstMCMB;
    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar) override
    {
        if (uMsg == DFM_MERGECONTEXTMENU_TOP)
            FirstMCMT = ((QCMINFO*)lPar)->idCmdFirst;
        if (uMsg == DFM_MERGECONTEXTMENU)
            FirstMCMM = ((QCMINFO*)lPar)->idCmdFirst;
        if (uMsg == DFM_MERGECONTEXTMENU_BOTTOM)
            FirstMCMB = ((QCMINFO*)lPar)->idCmdFirst;
        return MergePosCB(NULL, NULL, pDO, uMsg, wPar, lPar);
    }
    virtual void OnCreated() override
    {
        HMENU hMenu;
        ok_int(QueryContextMenu(pCM, TRUE, CMF_NORMAL, &hMenu) > 3 + 1, TRUE);
        ok_int(GetMenuPosFromID(hMenu, FirstMCMT + IDC_TOP), 0);
        ok_int(GetMenuPosFromID(hMenu, FirstMCMB + IDC_BOTTOM), GetMenuItemCount(hMenu) - 2); // Before "properties"
        ok(IsVerbByPos(pCM, "properties", hMenu, GetMenuItemCount(hMenu) - 1), "Properties is last\n");
        DestroyMenu(hMenu);
    }
    CDFMC2TEST_RUN(TF_NORMALFILE)


    CDFMC2TEST_DECLARE(MapCommandName)
    enum { IDC_MYCOMMAND = 42 };
    BOOL Passed;
    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar) override
    {
        if (uMsg == DFM_MAPCOMMANDNAME && !lstrcmpiW((LPWSTR)lPar, L"MyVerb"))
        {
            *((UINT*)wPar) = IDC_MYCOMMAND;
            return S_OK;
        }
        if (uMsg == DFM_MERGECONTEXTMENU)
        {
            return DfmInsert(*(QCMINFO*)lPar, IDC_MYCOMMAND, "My &Command");
        }
        if (uMsg == DFM_INVOKECOMMAND && wPar == IDC_MYCOMMAND)
        {
            Passed = TRUE;
            ok_int(lstrcmpiW((LPWSTR)lPar, L"MyParameter"), 0);
            return S_OK;
        }
        return DefaultHandler(uMsg);
    }
    virtual void OnCreated() override
    {
        CMINVOKECOMMANDINFO ici = { sizeof(ici), 0, NULL, "MyVerb", "MyParameter", NULL, SW_SHOW };
        Passed = FALSE;
        ok_int(pCM->InvokeCommand(&ici), S_OK);
        ok(Passed, "Must map the verb even without QueryContextMenu\n");

        Passed = FALSE;
        QueryContextMenu(pCM, TRUE);
        ok_int(pCM->InvokeCommand(&ici), S_OK);
        ok(Passed, "Must map the verb\n");

        ok(!IS_INTRESOURCE(ici.lpVerb), "InvokeCommand is not allowed to modify its input\n");
    }
    CDFMC2TEST_RUN(0)


    CDFMC2TEST_DECLARE(InvokeCommandEx)
    enum { Cmic = CMIC_MASK_FLAG_NO_UI, CmdId = IDC_MIDDLE };
    CMINVOKECOMMANDINFO *pICI;
    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar) override
    {
        if (uMsg == DFM_INVOKECOMMANDEX)
        {
            DFMICS *pics = (DFMICS*)lPar;
            ok_int(pics != NULL, TRUE);
            if (!pics)
                return E_INVALIDARG;
            ok(pics->cbSize >= FIELD_OFFSET(DFMICS, punkSite), ">= DFMICS.pici\n");
            ok_int(pics->fMask, Cmic);
            ok_eq_wstr((LPWSTR)pics->lParam, L"MyParameter");
            ok_int(pics->idCmdFirst, QCM_FIRST);
            ok_int(pics->pici != NULL, TRUE);
            if (pics->pici)
            {
                ok_int(pics->pici->fMask, Cmic);
                ok_eq_str(pics->pici->lpParameters, "MyParameter");
                ok_int(pics->pici->nShow, SW_SHOW);
            }
            return wPar == CmdId ? S_OK : E_UNEXPECTED;
        }
        return MergePosCB(NULL, NULL, pDO, uMsg, wPar, lPar);
    }
    virtual void OnCreated() override
    {
        CMINVOKECOMMANDINFO ici = { sizeof(ici), Cmic, NULL, (LPSTR)CmdId, "MyParameter", NULL, SW_SHOW };
        QueryContextMenu(pCM, TRUE);
        ok(SUCCEEDED(pCM->InvokeCommand(pICI = &ici)), "DFM_INVOKECOMMANDEX\n");
    }
    CDFMC2TEST_RUN(0)


    CDFMC2TEST_DECLARE(CallbackGcs)
    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar) override
    {
        if (uMsg == DFM_GETVERBA)
        {
            ok_int(lPar != 0, TRUE);
            ok_int(HIWORD(wPar) != 0, TRUE);
            if (LOWORD(wPar) == IDC_MIDDLE && lPar)
                return SHUnicodeToAnsi(L"MyVerb", (LPSTR)lPar, HIWORD(wPar)) ? S_OK : E_FAIL;
        }
        return MergePosCB(NULL, NULL, pDO, uMsg, wPar, lPar);
    }
    virtual void OnCreated() override
    {
        QueryContextMenu(pCM, TRUE);
        char buf[42] = {};
        ok_int(GetVerb(pCM, IDC_MIDDLE, buf, _countof(buf)), S_OK);
        ok_str(buf, "MyVerb");
    }
    CDFMC2TEST_RUN(0)


    CDFMC2TEST_DECLARE(FileGcsWithoutHKey)
    virtual void OnCreated() override
    {
        QueryContextMenu(pCM, TRUE);
        ok(HasFolderCommands(), "Must have folder commands\n");
        ok(HasFileCommands() == (GetWinMajor() >= 6), "Has file commands\n");
    }
    CDFMC2TEST_RUN(TF_SF | TF_APIDL)


    CDFMC2TEST_DECLARE(NoFcidmShviewGcs)
    virtual void OnCreated() override
    {
        if (QueryContextMenu(pCM, TRUE, CMF_NOVERBS) && HasFolderCommands())
            ok(!IsVerb(pCM, FCIDM_SHVIEW_PROPERTIES, "properties"), "No FcidmShview GCS\n");
        else
            skip("Unable to initialize test\n");
    }
    CDFMC2TEST_RUN(TF_NORMALFILE)


    CDFMC2TEST_DECLARE(DfmCmdCallbackMapGcs)
    virtual HRESULT Callback(IDataObject *pDO, UINT uMsg, WPARAM wPar, LPARAM lPar) override
    {
        if (uMsg == DFM_GETVERBA && LOWORD(wPar) == LOWORD(DFM_CMD_PROPERTIES) && lPar)
            return SHUnicodeToAnsi(L"MyVerb", (LPSTR)lPar, HIWORD(wPar)) ? S_OK : E_FAIL;
        return DefaultHandler(uMsg);
    }
    virtual void OnCreated() override
    {
        QueryContextMenu(pCM, TRUE, CMF_NOVERBS);
        ok(IsVerb(pCM, (SIZE_T)"properties", "MyVerb"), "GCS DfmCmd map\n");
    }
    CDFMC2TEST_RUN(TF_NORMALFILE)
}

struct ContextMenuCBBase : IContextMenuCB
{
    virtual ~ContextMenuCBBase() {}
    virtual ULONG WINAPI AddRef() { return 2; }
    virtual ULONG WINAPI Release() { return 1; }
    virtual HRESULT WINAPI QueryInterface(REFIID riid, void**ppv)
    {
        if (riid == IID_IContextMenuCB || riid == IID_IUnknown)
        {
            AddRef();
            *ppv = (void*) this;
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    virtual HRESULT WINAPI CallBack(IShellFolder*, HWND, IDataObject*, UINT, WPARAM, LPARAM) = 0;
};

struct ContextMenuMessagesDefaultCB : ContextMenuCBBase
{
    virtual HRESULT WINAPI CallBack(IShellFolder *pSF, HWND hWnd, IDataObject *pDO, UINT Msg, WPARAM wPar, LPARAM lPar)
    {
        BeginContextMenuCallBack(Msg, lPar);
        return SHELL32_DefaultContextMenuCallBack(Msg);
    }
};

void SHCreateDefaultContextMenuTests(IShellFolder *pSF, PCUITEMID_CHILD pidlChild)
{
    HRESULT hr;

    do
    {
        ContextMenuMessagesDefaultCB cbobj;
        DEFCONTEXTMENU dfm = { NULL, &cbobj, NULL, pSF, 1, &pidlChild, NULL, 0, NULL };
        CComPtr<IContextMenu> pCM;
        hr = Begin(g_SHCDCM(&dfm, IID_PPV_ARG(IContextMenu, &pCM)));
        ok_int(SUCCEEDED(hr), TRUE);
        if (FAILED(hr))
            break;
        ok_int(Stat_GetMessageIndex(/*DFM_?*/ 3), 0); // This unknown message is sent first
        ok_int(QueryContextMenu(pCM, TRUE) > 0, TRUE);
        ok_int(Stat_GotMessage(DFM_MODIFYQCMFLAGS), TRUE);
        ok_int(Stat_GetMessageIndex(DFM_MODIFYQCMFLAGS) < Stat_GetMessageIndex(DFM_MERGECONTEXTMENU), TRUE);
    } while (FALSE);
}

START_TEST(DefaultContextMenu)
{
    CCoInit ComStaInit;
    HMODULE hShell32 = LoadLibraryA("SHELL32");
    g_SHCDCM = (SHCREATEDEFAULTCONTEXTMENU)GetProcAddress(hShell32, "SHCreateDefaultContextMenu");
    g_hMsgStats = DPA_Create(0);

    WCHAR szPath[MAX_PATH];
    GetSystemDirectoryW(szPath, _countof(szPath));
    CComHeapPtr<ITEMIDLIST> pidlSysDir(ILCreateFromPathW(szPath)), pidlFile, pidlTemp;
    CComPtr<IShellFolder> pNormalShellFolder;
    CComPtr<IShellFolder> pSF;
    PCUITEMID_CHILD pidlLast;
    if (pidlSysDir && SUCCEEDED(SHBindToParent(pidlSysDir, IID_PPV_ARG(IShellFolder, &pSF), &pidlLast)))
        pSF->BindToObject(pidlLast, NULL, IID_PPV_ARG(IShellFolder, &pNormalShellFolder));
    pidlTemp.Attach(SHSimpleIDListFromPath(L"x:\\explorer.exe"));
    pidlFile.Attach(ILCombine(pidlSysDir, ILFindLastID(pidlTemp)));
    PCUITEMID_CHILD pidlFileLast = ILFindLastID(pidlFile);
    g_pNormalFolder = pNormalShellFolder;
    g_pFileLastId = pidlFileLast;

    if (pNormalShellFolder && pidlFileLast)
        BasicTests(pNormalShellFolder, pidlFileLast);
    else
        skip("Unable to initialize test\n");

    if (g_SHCDCM)
        SHCreateDefaultContextMenuTests(pNormalShellFolder, pidlFileLast);
    else
        skip("%s not implemented\n", "SHCreateDefaultContextMenu");

    DPA_Destroy(g_hMsgStats);
}
