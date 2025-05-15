#pragma once

#define USE_CUSTOM_MENUBAND 1
#define USE_CUSTOM_MERGEDFOLDER 1
#define USE_CUSTOM_ADDRESSBAND 1
#define USE_CUSTOM_ADDRESSEDITBOX 1
#define USE_CUSTOM_BANDPROXY 1
#define USE_CUSTOM_BRANDBAND 1
#define USE_CUSTOM_EXPLORERBAND 0 // Moved to shdocvw.dll
#define USE_CUSTOM_SEARCHBAND 1
#define USE_CUSTOM_INTERNETTOOLBAR 1

#define ITB_INVALID INT_MAX

#define THISMODULE_RESINSTANCE _AtlBaseModule.GetResourceInstance()

HMENU LoadSubMenu(UINT ResId, UINT nPos);

typedef struct _ACCELTABLE
{
    BYTE Vk;
    BYTE Mods;
    WORD Id;
} ACCELTABLE;

int IsAccelerator(LPMSG pMsg, const ACCELTABLE *pAT, UINT cAT);

HRESULT IOleWindow_UIActivateIO(_In_ IOleWindow *pOW, _Out_opt_ HWND *phWnd, _In_opt_ LPMSG pMsg);

HRESULT CAddressBand_CreateInstance(REFIID riid, void **ppv);
HRESULT CAddressEditBox_CreateInstance(REFIID riid, void **ppv);
HRESULT CBandProxy_CreateInstance(REFIID riid, void **ppv);
HRESULT CBrandBand_CreateInstance(REFIID riid, void **ppv);
HRESULT CExplorerBand_CreateInstance(REFIID riid, LPVOID *ppv);
HRESULT CSearchBar_CreateInstance(REFIID riid, LPVOID *ppv);
HRESULT CInternetToolbar_CreateInstance(REFIID riid, void **ppv);
HRESULT CMergedFolder_CreateInstance(REFIID riid, void **ppv);
HRESULT CMenuBand_CreateInstance(REFIID iid, LPVOID *ppv);
HRESULT CShellBrowser_CreateInstance(REFIID riid, void **ppv);
HRESULT CTravelLog_CreateInstance(REFIID riid, void **ppv);
HRESULT CBaseBar_CreateInstance(REFIID riid, void **ppv, BOOL vertical);
HRESULT CBaseBarSite_CreateInstance(REFIID riid, void **ppv, BOOL bVertical);
HRESULT CToolsBand_CreateInstance(REFIID riid, void **ppv);
HRESULT IEGetNameAndFlags(LPITEMIDLIST pidl, SHGDNF uFlags, LPWSTR pszBuf, UINT cchBuf, SFGAOF *rgfInOut);
