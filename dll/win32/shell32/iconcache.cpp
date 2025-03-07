/*
 * PROJECT:     ReactOS shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     System Image List (SIL) / Shell Icon Cache (SIC)
 * COPYRIGHT:   Copyright 1998, 1999 Juergen Schmied
 *              Copyright 2025 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 *              Copyright 2025 Whindmar Saksit <whindsaks@proton.me>
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

EXTERN_C BOOL PathIsExeW(LPCWSTR lpszPath);

/********************** THE ICON CACHE ********************************/

#define SICLINKOVERLAYHACK TRUE // FIXME
#if SICLINKOVERLAYHACK
#define GIL_CACHEMASK_DOCUMENTED (GIL_SIMULATEDOC | GIL_NOTFILENAME) // SHUpdateImage
#define GIL_CACHEMASK (GIL_FORSHORTCUT | GIL_NOTFILENAME) // FIXME: Change to GIL_CACHEMASK_DOCUMENTED
#else
#define GIL_CACHEMASK (GIL_SIMULATEDOC | GIL_NOTFILENAME) // SHUpdateImage
#endif
#define MAXSILCOUNT (1 + SHIL_JUMBO)

static const WCHAR g_pszShell32DotDll[] = L"shell32.dll";
static INT g_SIL_NoAssocIndex = INVALID_INDEX;

typedef struct
{
    PCWSTR sSourceFile;    /* file (not path!) containing the icon */
    DWORD dwSourceIndex;    /* index within the file, if it is a resoure ID it will be negated */
    DWORD dwListIndex;    /* index within the iconlist */
    DWORD dwFlags;        /* GIL_* flags */
    DWORD dwAccessTime;
} SIC_ENTRY, * LPSIC_ENTRY;

static HDPA        sic_hdpa = 0;
static signed char g_InitState = 0;
static UINT8       g_ListCount = 0;
static HIMAGELIST  g_hSIL[MAXSILCOUNT] = {};
static WORD        g_SILSizes[MAXSILCOUNT] = {};
static const BYTE  g_SizePriority[] = { SHIL_JUMBO, SHIL_EXTRALARGE, SHIL_LARGE, SHIL_SYSSMALL, SHIL_SMALL };
C_ASSERT(_countof(g_SizePriority) == MAXSILCOUNT);

namespace
{
extern CRITICAL_SECTION SHELL32_SicCS;
CRITICAL_SECTION_DEBUG critsect_debug =
{
    0, 0, &SHELL32_SicCS,
    { &critsect_debug.ProcessLocksList, &critsect_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": SHELL32_SicCS") }
};
CRITICAL_SECTION SHELL32_SicCS = { &critsect_debug, -1, 0, 0, 0, 0 };
}

static HICON SIC_OverlayShortcutImage(HICON SourceIcon, UINT ListId);
static int SIC_LoadOverlayIcon(int icon_idx);

#define SIC_IsInitialized() ( g_InitState > 0 )
#define SIC_EnsureInitialized() ( SIC_IsInitialized() ? S_OK : SIC_TryInitialize() )
#define SIL_FastGetIconSize(Id) g_SILSizes[Id]
#define SIL_GetListCount() g_ListCount
#define SIC_GetFallbackSimulateDocCachedIndex() SIC_GetDefaultOverridableCachedIndex(SIID_DOCASSOC)
#define SIC_GetFallbackExeCachedIndex() SIC_GetDefaultOverridableCachedIndex(SIID_APPLICATION)

static inline HRESULT GetIconLocationFromEI(IExtractIconW *pEIW, IExtractIconA *pEIA, UINT GilIn,
                                            PWSTR OutW, UINT CapW, PSTR OutA, int *pIdx, UINT *pGilOut)
{
#if SICLINKOVERLAYHACK
    GilIn &= ~GIL_FORSHORTCUT; // Cannot pass this flag, CShellLink::GetIconLocation thinks it's recursing on itself.
#endif
    if (!pGilOut || *pGilOut)
        WARN("pGilOut %s input\n", !pGilOut ? "invalid" : "problematic");
    if (pEIW)
        return pEIW->GetIconLocation(GilIn, OutW, CapW, pIdx, pGilOut);
    ASSERT(pEIA);
    OutA[0] = ANSI_NULL;
    HRESULT hr = pEIA->GetIconLocation(GilIn, OutA, MAX_PATH, pIdx, pGilOut);
    return SHAnsiToUnicode(OutA, OutW, CapW) ? hr : HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
}

static HRESULT SIC_TryInitialize()
{
    HRESULT hr = S_OK;
    // We have to be careful so we don't call SIC_Initialize recursively!
    EnterCriticalSection(&SHELL32_SicCS);
    if (!SIC_IsInitialized() && !g_InitState)
        hr = SIC_Initialize(FALSE);
    LeaveCriticalSection(&SHELL32_SicCS);
    return hr;
}

EXTERN_C HIMAGELIST SIL_GetImageList(UINT Id)
{
    return SIC_EnsureInitialized() == S_OK && Id < SIL_GetListCount() ? g_hSIL[Id] : NULL;
}

EXTERN_C UINT SIL_GetIconSize(UINT Id)
{
    return Id < SIL_GetListCount() ? SIL_FastGetIconSize(Id) : 0;
}

static inline bool SIC_IsShell32DllPath(PCWSTR Path)
{
    return !_wcsicmp(PathFindFileNameW(Path), g_pszShell32DotDll);
}

static PCWSTR SIC_CreateCacheEntryPath(PCWSTR Path)
{
    // Shell32.dll is special and stored without a path (for registry compatibility)
    // Iconcache.db on NT 5 stores Shell32.dll without a path and does not %unexpand%.
    return SIC_IsShell32DllPath(Path) ? g_pszShell32DotDll : Path;
}

static INT SIC_ReadMetricsValue(PCWSTR pszValueName, INT nDefaultValue)
{
    WCHAR szValue[64];
    DWORD cbValue = sizeof(szValue);
    DWORD error = SHGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics",
                              pszValueName, NULL, szValue, &cbValue);
    if (error)
        return nDefaultValue;
    szValue[_countof(szValue) - 1] = UNICODE_NULL; // Avoid buffer overrun
    return _wtoi(szValue);
}

static inline INT SIL_GetIconSizeMetric(PCWSTR Name, UINT DefaultSysMetric)
{
    INT nDefaultSize = GetSystemMetrics(DefaultSysMetric);
    INT nIconSize = SIC_ReadMetricsValue(Name, nDefaultSize);
    return (nIconSize > 0) ? nIconSize : nDefaultSize;
}

static inline INT SIL_GetIconBppMetric(VOID) // Bits Per Pixel
{
    INT nDefaultBPP = SHGetCurColorRes();
    INT nIconBPP = SIC_ReadMetricsValue(L"Shell Icon BPP", nDefaultBPP);
    return (nIconBPP > 0) ? nIconBPP : nDefaultBPP;
}

static UINT SIL_ReadIconMetrics(WORD Sizes[MAXSILCOUNT])
{
    Sizes[SHIL_LARGE] = SIL_GetIconSizeMetric(L"Shell Icon Size", SM_CXICON);
    Sizes[SHIL_SMALL] = SIL_GetIconSizeMetric(L"Shell Small Icon Size", SM_CXSMICON);
    if (MAXSILCOUNT > SHIL_EXTRALARGE)
        Sizes[SHIL_EXTRALARGE] = GetSystemMetrics(SM_CXICON) * 3 / 2; // 48x48
    if (MAXSILCOUNT > SHIL_SYSSMALL)
        Sizes[SHIL_SYSSMALL] = GetSystemMetrics(SM_CXSMICON);
    if (MAXSILCOUNT > SHIL_JUMBO)
        Sizes[SHIL_JUMBO] = 256;
    return SIL_GetIconBppMetric();
}

/*****************************************************************************
 * SIC_CompareEntries
 *
 * NOTES
 *  Callback for DPA_Search
 */
static INT CALLBACK SIC_CompareEntries( LPVOID p1, LPVOID p2, LPARAM lparam)
{    LPSIC_ENTRY e1 = (LPSIC_ENTRY)p1, e2 = (LPSIC_ENTRY)p2;

    TRACE("%p %p %8lx\n", p1, p2, lparam);

    /* Icons in the cache are keyed by the name of the file they are
     * loaded from, their resource index and the fact if they have a shortcut
     * icon overlay or not.
     */
    /* first the faster one */
    if (e1->dwSourceIndex != e2->dwSourceIndex)
        return (e1->dwSourceIndex < e2->dwSourceIndex) ? -1 : 1;

    ASSERT(((e1->dwFlags | e2->dwFlags) & ~GIL_CACHEMASK) == 0);
    if (e1->dwFlags != e2->dwFlags)
      return (e1->dwFlags < e2->dwFlags) ? -1 : 1;
    return _wcsicmp(e1->sSourceFile,e2->sSourceFile);
}

static void SIC_InitLookupEntry(SIC_ENTRY &sice, PCWSTR Path, int iIcon, UINT GilOut)
{
    sice.dwSourceIndex = iIcon;
    sice.dwFlags = GilOut & GIL_CACHEMASK;
    sice.sSourceFile = SIC_CreateCacheEntryPath(Path);
}

#define SIC_FreeEntry SHFree
static inline LPSIC_ENTRY SIC_AllocEntry(const SIC_ENTRY &sice, int ListIndex)
{
    ASSERT(sice.sSourceFile);
    SIZE_T cch = wcslen(sice.sSourceFile) + 1, cbFile = cch * sizeof(WCHAR);
    LPSIC_ENTRY pEntry = (LPSIC_ENTRY)SHAlloc(sizeof(*pEntry) + cbFile);
    if (!pEntry)
        return pEntry;
    CopyMemory(pEntry, &sice, sizeof(*pEntry));
    pEntry->sSourceFile = reinterpret_cast<PWSTR>((char*)pEntry + sizeof(*pEntry));
    pEntry->dwListIndex = ListIndex;
    CopyMemory(const_cast<PWSTR>(pEntry->sSourceFile), sice.sSourceFile, cbFile);
    return pEntry;
}

static inline LPSIC_ENTRY SIC_LookupIconEntryInternal(SIC_ENTRY &sice)
{
    ASSERT((sice.dwFlags & ~GIL_CACHEMASK) == 0);
    int index = DPA_Search(sic_hdpa, &sice, 0, SIC_CompareEntries, 0, DPAS_SORTED);
    return index != -1 ? (LPSIC_ENTRY)DPA_GetPtr(sic_hdpa, index) : NULL;
}

static inline int SIC_LookupIconIndexInternal(SIC_ENTRY &sice)
{
    LPSIC_ENTRY p = SIC_LookupIconEntryInternal(sice);
    return p ? p->dwListIndex : INVALID_INDEX;
}

static int SIC_TryLookupIconIndex(SIC_ENTRY &sice)
{
    int iList = INVALID_INDEX;
    EnterCriticalSection(&SHELL32_SicCS);
    if (SIC_IsInitialized())
        iList = SIC_LookupIconIndexInternal(sice);
    LeaveCriticalSection(&SHELL32_SicCS);
    return iList;
}

static inline int SIC_TryLookupIconIndex(PCWSTR Path, int iIcon, UINT GilOut)
{
    SIC_ENTRY sice;
    SIC_InitLookupEntry(sice, Path, iIcon, GilOut);
    return SIC_TryLookupIconIndex(sice);
}

static void SIC_SetLookupIconIndex(PCWSTR Path, int iIcon, UINT GilOut, UINT iList)
{
    if (GilOut & GIL_DONTCACHE)
        return;

    SIC_ENTRY sice;
    SIC_InitLookupEntry(sice, Path, iIcon, GilOut);
    EnterCriticalSection(&SHELL32_SicCS);
    if (SIC_IsInitialized())
    {
        LPSIC_ENTRY p = SIC_LookupIconEntryInternal(sice);
        if (p)
            p->dwListIndex = iList;
    }
    LeaveCriticalSection(&SHELL32_SicCS);
}

static HRESULT SIC_ExtractHelper(HICON *pIco, IExtractIconW *pEIW, IExtractIconA *pEIA, int hr,
                                 PCWSTR szW, PCSTR szA, int iIcon, UINT GilOut, UINT Size)
{
    HICON *phIco2 = &pIco[1];
    pIco[0] = NULL;
    if (!HIWORD(Size))
        phIco2 = NULL;
    else
        pIco[1] = NULL;

    if (hr == S_OK && !(GilOut & GIL_NOTFILENAME) && *szW)
    {
        // IExtractIcon::GetIconLocation claims to be valid, we can extract directly.
        hr = S_FALSE;
    }
    else if (pEIW)
    {
        hr = pEIW->Extract(szW, iIcon, &pIco[0], phIco2, Size);
    }
    else if (pEIA)
    {
        CHAR szBufA[MAX_PATH];
        if (!szA && SHUnicodeToAnsi(szW, szBufA, _countof(szBufA)))
            szA = szBufA;
        if (szA)
            hr = pEIA->Extract(szA, iIcon, &pIco[0], phIco2, Size);
    }

    if (hr == S_FALSE && !(GilOut & GIL_NOTFILENAME))
    {
        #if 01
        // SHDefExtractIconW (PrivateExtractIconsW) does not correctly extract two sizes from .ico files!
        // This bug is ROS specific and we can change to a single call when that is fixed.
        // It even returns a non-NULL invalid handle value, be careful!
        // TODO: hr = SHDefExtractIconW(szW, iIcon, GilOut, &pIco[0], phIco2, Size);
        hr = SHDefExtractIconW(szW, iIcon, GilOut, &pIco[0], NULL, LOWORD(Size));
//DbgPrint("%#x:%ls|\n", hr, szW);
        if (hr == S_OK && phIco2)
            hr = SHDefExtractIconW(szW, iIcon, GilOut, phIco2, NULL, HIWORD(Size));
        #else
        // We want to extract two icons at once but we can't trust SHDefExtractIconW (PrivateExtractIconsW)
        // and even if we could, ROS does not extract them correctly for 16x16 icon #2 ?!?!
        hr = SHDefExtractIconW(szW, iIcon, GilOut, &pIco[0], phIco2, Size);
DbgPrint("%#x:%ls| %p %p\n", hr, szW, pIco[0], pIco[1]);
        if (phIco2 && (!*phIco2 || !pIco[0]))
        {
            if (!pIco[0])
                hr = SHDefExtractIconW(szW, iIcon, GilOut, &pIco[0], NULL, LOWORD(Size));
            if (!*phIco2)
                hr = SHDefExtractIconW(szW, iIcon, GilOut, phIco2, NULL, HIWORD(Size));
        }
        #endif
    }

    if (!pIco[0] && SUCCEEDED(hr))
    {
        if (phIco2)
        {
            if (*phIco2)
                DestroyIcon(*phIco2);
            *phIco2 = NULL;
        }
        if (hr == S_OK)
            hr = S_FALSE;
    }
    return hr;
}

static HRESULT SIC_AddIconInternal(SIC_ENTRY &sice, HICON *phIcons, UINT GilLnkHack)
{
    ASSERT(SIC_LookupIconIndexInternal(sice) < 0);
    HRESULT hr = S_OK;
    int iCanonicalIndex = 0, iIndex, iFallbackIcon = -1;
    UINT AddedCount = 0, ListsCount = SIL_GetListCount();

    for (UINT i = 0; i < ListsCount; ++i)
    {
        HICON hIco = phIcons[i];
        if (!hIco) // If one of the icons are invalid (unsupported PNG etc), find a fallback.
        {
            for (UINT j = 0; iFallbackIcon < 0 && j < _countof(g_SizePriority); ++j)
            {
                if (phIcons[j])
                    iFallbackIcon = j;
            }
            hIco = phIcons[iFallbackIcon];
        }
#if SICLINKOVERLAYHACK
        HICON hIcoLnk = NULL;
        if (GilLnkHack & GIL_FORSHORTCUT)
            hIcoLnk = SIC_OverlayShortcutImage(hIco, i);
        iIndex = ImageList_AddIcon(g_hSIL[i], hIcoLnk ? hIcoLnk : hIco);
        if (hIcoLnk)
            DestroyIcon(hIcoLnk);
#else
        iIndex = ImageList_AddIcon(g_hSIL[i], hIco);
#endif
        if (iIndex < 0 || iIndex != iCanonicalIndex)
        {
            if (i != 0)
            {
                WARN("Unable to add icon at the same index, aborting\n");
                hr = E_OUTOFMEMORY; // We were unable to add the Nth icon. Stop adding icons.
                break;
            }
            iCanonicalIndex = iIndex;
        }
        ++AddedCount;
    }

    if (ListsCount != AddedCount)
        hr = E_FAIL; // All lists must have the same count, abort and undo!

    if (SUCCEEDED(hr))
    {
        hr = E_OUTOFMEMORY;
        if (LPSIC_ENTRY lpsice = SIC_AllocEntry(sice, iCanonicalIndex))
        {
            iIndex = DPA_Search(sic_hdpa, lpsice, 0, SIC_CompareEntries, 0, DPAS_SORTED | DPAS_INSERTAFTER);
            iIndex = DPA_InsertPtr(sic_hdpa, iIndex, lpsice);
            if (iIndex != -1)
                hr = S_OK;
            else
                SHFree(lpsice);

            ASSERT(hr || SIC_LookupIconIndexInternal(*lpsice) == iCanonicalIndex);
        }
    }

    for (UINT i = 0; FAILED(hr) && i < AddedCount; ++i)
        ImageList_Remove(g_hSIL[i], iCanonicalIndex);

    ASSERT(ImageList_GetImageCount(g_hSIL[0]) == ImageList_GetImageCount(g_hSIL[1]));
    return SUCCEEDED(hr) ? iCanonicalIndex : hr;
}

template<class F, class CD>
static HRESULT SIC_FindOrAddIcon(F Func, CD CallerData, PCWSTR pszIcon, int iIcon, UINT GilOut)
{
    SIC_ENTRY sice;
    SIC_InitLookupEntry(sice, pszIcon, iIcon, GilOut);

    int iListIndex = INVALID_INDEX;
    if (!(GilOut & GIL_DONTCACHE) && (iListIndex = SIC_TryLookupIconIndex(sice)) >= 0)
        return iListIndex;

    HRESULT hr = SIC_EnsureInitialized();
    if (FAILED(hr))
        return hr;

    // Extract all the icons (outside the lock)
    const UINT ListCount = SIL_GetListCount();
    UINT HasIcon = 0;
    HICON hIcons[MAXSILCOUNT] = {};
    for (UINT i = 0, Sizes, Offset; i < ListCount;)
    {
        Sizes = SIL_FastGetIconSize(Offset = i++);
        if (i < ListCount)
            Sizes = MAKELONG(Sizes, SIL_FastGetIconSize(i++));
        hr = Func(&hIcons[Offset], pszIcon, iIcon, GilOut, Sizes, CallerData);
        HasIcon |= (hr == S_OK);
        if (!HasIcon && hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
            break;
    }

    if (HasIcon)
    {
        EnterCriticalSection(&SHELL32_SicCS);
        // Extracting might have taken some time, check the cache again inside the lock.
        if (!(GilOut & GIL_DONTCACHE))
            iListIndex = SIC_LookupIconIndexInternal(sice);
        // If we have a unique path+index+flags tuple, add the icon to the imagelists and cache.
        if (iListIndex < 0)
            hr = SIC_AddIconInternal(sice, hIcons, GilOut);
        LeaveCriticalSection(&SHELL32_SicCS);
    }

    for (UINT i = 0; i < _countof(hIcons) && hIcons[i]; ++i)
        DestroyIcon(hIcons[i]);
    return hr;
}

static HRESULT EIFindOrAddIconExtractor(HICON*phIcons, PCWSTR pszIco, int iIcon,
                                            UINT GilOut, UINT Sizes, SIZE_T *pData)
{
    IExtractIconW *pEIW = (IExtractIconW*)pData[0];
    IExtractIconA *pEIA = (IExtractIconA*)pData[1];
    return SIC_ExtractHelper(phIcons, pEIW, pEIA, (UINT)pData[3], pszIco, (PSTR)(pData[2]), iIcon, GilOut, Sizes);
}

static inline HRESULT SIC_FindOrAddIcon(IExtractIconW *pEIW, IExtractIconA *pEIA, int hr,
                                        PCWSTR szW, PCSTR szA, int iIcon, UINT GilOut)
{
    SIZE_T data[] = { (SIZE_T)pEIW, (SIZE_T)pEIA, (SIZE_T)szA, (SIZE_T)hr };
    return SIC_FindOrAddIcon(EIFindOrAddIconExtractor, data, szW, iIcon, GilOut);
}

static inline HRESULT SIC_FindOrAddIconFromLocation(PCWSTR Path, int iIcon, UINT GilOut)
{
    return SIC_FindOrAddIcon(NULL, NULL, S_FALSE, Path, NULL, iIcon, GilOut);
}

#if UNUSED && FALSE
static HRESULT ModuleFindOrAddIconExtractor(HICON*phIcons, PCWSTR pszIco, int iIcon,
                                                UINT GilOut, UINT Sizes, SIZE_T *pData)
{
    UINT i = 0, Size = LOWORD(Sizes);
    for (; Size != 0; Size = Sizes >>= 16)
    {
        HICON hIco = (HICON)LoadImageW((HMODULE)pData[0], MAKEINTRESOURCEW(pData[2]),
                                       IMAGE_ICON, Size, Size, (UINT)pData[1] & ~LR_SHARED);
        if ((phIcons[i++] = hIco) == NULL)
            return E_FAIL;
    }
    return i ? S_OK : S_FALSE;
}

static inline HRESULT SIC_FindOrAddIconFromModule(PCWSTR Path, HMODULE hMod, int ResId, UINT LR)
{
    SIZE_T data[] = { (SIZE_T)hMod, LR, HIWORD(ResId) };
    return SIC_FindOrAddIcon(ModuleFindOrAddIconExtractor, data, Path, (SHORT)LOWORD(ResId), 0);
}
#endif

/*****************************************************************************
 * SIC_OverlayShortcutImage            [internal]
 *
 * NOTES
 *  Creates a new icon as a copy of the passed-in icon, overlayed with a
 *  shortcut image.
 * FIXME: This should go to the ImageList implementation!
 */
static HICON SIC_OverlayShortcutImage(HICON SourceIcon, UINT ListId)
{
    ICONINFO ShortcutIconInfo, TargetIconInfo;
    HICON ShortcutIcon = NULL, TargetIcon;
    BITMAP TargetBitmapInfo, ShortcutBitmapInfo;
    HDC ShortcutDC = NULL,
      TargetDC = NULL;
    HBITMAP OldShortcutBitmap = NULL,
      OldTargetBitmap = NULL;

    static int s_imgListIdx = -1;
    ZeroMemory(&ShortcutIconInfo, sizeof(ShortcutIconInfo));
    ZeroMemory(&TargetIconInfo, sizeof(TargetIconInfo));

    /* Get information about the source icon and shortcut overlay.
     * We will write over the source bitmaps to get the final ones */
    if (! GetIconInfo(SourceIcon, &TargetIconInfo))
        return NULL;

    /* Is it possible with the ImageList implementation? */
    if(!TargetIconInfo.hbmColor)
    {
        /* Maybe we'll support this at some point */
        FIXME("1bpp icon wants its overlay!\n");
        goto fail;
    }

    if(!GetObjectW(TargetIconInfo.hbmColor, sizeof(BITMAP), &TargetBitmapInfo))
    {
        goto fail;
    }

    /* search for the shortcut icon only once */
    if (s_imgListIdx == -1)
    {
        EnterCriticalSection(&SHELL32_SicCS);
        s_imgListIdx = SIC_LoadOverlayIcon(IDI_SHELL_SHORTCUT - 1);
        LeaveCriticalSection(&SHELL32_SicCS);
    }

    if (s_imgListIdx >= 0)
        ShortcutIcon = ImageList_GetIcon(g_hSIL[ListId], s_imgListIdx, ILD_TRANSPARENT);
    else
        ShortcutIcon = NULL;

    if (!ShortcutIcon || !GetIconInfo(ShortcutIcon, &ShortcutIconInfo))
    {
        goto fail;
    }

    /* Is it possible with the ImageLists ? */
    if(!ShortcutIconInfo.hbmColor)
    {
        /* Maybe we'll support this at some point */
        FIXME("Should draw 1bpp overlay!\n");
        goto fail;
    }

    if(!GetObjectW(ShortcutIconInfo.hbmColor, sizeof(BITMAP), &ShortcutBitmapInfo))
    {
        goto fail;
    }

    /* Setup the masks */
    ShortcutDC = CreateCompatibleDC(NULL);
    if (NULL == ShortcutDC) goto fail;
    OldShortcutBitmap = (HBITMAP)SelectObject(ShortcutDC, ShortcutIconInfo.hbmMask);
    if (NULL == OldShortcutBitmap) goto fail;

    TargetDC = CreateCompatibleDC(NULL);
    if (NULL == TargetDC) goto fail;
    OldTargetBitmap = (HBITMAP)SelectObject(TargetDC, TargetIconInfo.hbmMask);
    if (NULL == OldTargetBitmap) goto fail;

    /* Create the complete mask by ANDing the source and shortcut masks.
     * NOTE: in an ImageList, all icons have the same dimensions */
    if (!BitBlt(TargetDC, 0, 0, ShortcutBitmapInfo.bmWidth, ShortcutBitmapInfo.bmHeight,
                ShortcutDC, 0, 0, SRCAND))
    {
      goto fail;
    }

    /*
     * We must remove or add the alpha component to the shortcut overlay:
     * If we don't, SRCCOPY will copy it to our resulting icon, resulting in a
     * partially transparent icons where it shouldn't be, and to an invisible icon
     * if the underlying icon don't have any alpha channel information. (16bpp only icon for instance).
     * But if the underlying icon has alpha channel information, then we must mark the overlay information
     * as opaque.
     * NOTE: This code sucks(tm) and should belong to the ImageList implementation.
     * NOTE2: there are better ways to do this.
     */
    if(ShortcutBitmapInfo.bmBitsPixel == 32)
    {
        BOOL add_alpha;
        BYTE buffer[sizeof(BITMAPINFO) + 256 * sizeof(RGBQUAD)];
        BITMAPINFO* lpbmi = (BITMAPINFO*)buffer;
        PVOID bits;
        PULONG pixel;
        INT i, j;

        /* Find if the source bitmap has an alpha channel */
        if(TargetBitmapInfo.bmBitsPixel != 32) add_alpha = FALSE;
        else
        {
            ZeroMemory(buffer, sizeof(buffer));
            lpbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            lpbmi->bmiHeader.biWidth = TargetBitmapInfo.bmWidth;
            lpbmi->bmiHeader.biHeight = TargetBitmapInfo.bmHeight;
            lpbmi->bmiHeader.biPlanes = 1;
            lpbmi->bmiHeader.biBitCount = 32;

            bits = HeapAlloc(GetProcessHeap(), 0, TargetBitmapInfo.bmHeight * TargetBitmapInfo.bmWidthBytes);

            if(!bits) goto fail;

            if(!GetDIBits(TargetDC, TargetIconInfo.hbmColor, 0, TargetBitmapInfo.bmHeight, bits, lpbmi, DIB_RGB_COLORS))
            {
                ERR("GetBIBits failed!\n");
                HeapFree(GetProcessHeap(), 0, bits);
                goto fail;
            }

            i = j = 0;
            pixel = (PULONG)bits;

            for(i=0; i<TargetBitmapInfo.bmHeight; i++)
            {
                for(j=0; j<TargetBitmapInfo.bmWidth; j++)
                {
                    add_alpha = (*pixel++ & 0xFF000000) != 0;
                    if(add_alpha) break;
                }
                if(add_alpha) break;
            }
            HeapFree(GetProcessHeap(), 0, bits);
        }

        /* Allocate the bits */
        bits = HeapAlloc(GetProcessHeap(), 0, ShortcutBitmapInfo.bmHeight*ShortcutBitmapInfo.bmWidthBytes);
        if(!bits) goto fail;

        ZeroMemory(buffer, sizeof(buffer));
        lpbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        lpbmi->bmiHeader.biWidth = ShortcutBitmapInfo.bmWidth;
        lpbmi->bmiHeader.biHeight = ShortcutBitmapInfo.bmHeight;
        lpbmi->bmiHeader.biPlanes = 1;
        lpbmi->bmiHeader.biBitCount = 32;

        if(!GetDIBits(TargetDC, ShortcutIconInfo.hbmColor, 0, ShortcutBitmapInfo.bmHeight, bits, lpbmi, DIB_RGB_COLORS))
        {
            ERR("GetBIBits failed!\n");
            HeapFree(GetProcessHeap(), 0, bits);
            goto fail;
        }

        pixel = (PULONG)bits;
        /* Remove alpha channel component or make it totally opaque */
        for(i=0; i<ShortcutBitmapInfo.bmHeight; i++)
        {
            for(j=0; j<ShortcutBitmapInfo.bmWidth; j++)
            {
                if(add_alpha) *pixel++ |= 0xFF000000;
                else *pixel++ &= 0x00FFFFFF;
            }
        }

        /* GetDIBits return BI_BITFIELDS with masks set to 0, and SetDIBits fails when masks are 0. The irony... */
        lpbmi->bmiHeader.biCompression = BI_RGB;

        /* Set the bits again */
        if(!SetDIBits(TargetDC, ShortcutIconInfo.hbmColor, 0, ShortcutBitmapInfo.bmHeight, bits, lpbmi, DIB_RGB_COLORS))
        {
            ERR("SetBIBits failed!, %lu\n", GetLastError());
            HeapFree(GetProcessHeap(), 0, bits);
            goto fail;
        }
        HeapFree(GetProcessHeap(), 0, bits);
    }

    /* Now do the copy. We overwrite the original icon data */
    if (NULL == SelectObject(ShortcutDC, ShortcutIconInfo.hbmColor) ||
        NULL == SelectObject(TargetDC, TargetIconInfo.hbmColor))
        goto fail;
    if (!MaskBlt(TargetDC, 0, 0, ShortcutBitmapInfo.bmWidth, ShortcutBitmapInfo.bmHeight,
                 ShortcutDC, 0, 0, ShortcutIconInfo.hbmMask, 0, 0,
                 MAKEROP4(0xAA0000, SRCCOPY)))
    {
        goto fail;
    }

    /* Clean up, we're not goto'ing to 'fail' after this so we can be lazy and not set
       handles to NULL */
    SelectObject(TargetDC, OldTargetBitmap);
    DeleteDC(TargetDC);
    SelectObject(ShortcutDC, OldShortcutBitmap);
    DeleteDC(ShortcutDC);

    /* Create the icon using the bitmaps prepared earlier */
    TargetIcon = CreateIconIndirect(&TargetIconInfo);

    /* CreateIconIndirect copies the bitmaps, so we can release our bitmaps now */
    DeleteObject(TargetIconInfo.hbmColor);
    DeleteObject(TargetIconInfo.hbmMask);
    /* Delete what GetIconInfo gave us */
    DeleteObject(ShortcutIconInfo.hbmColor);
    DeleteObject(ShortcutIconInfo.hbmMask);
    DestroyIcon(ShortcutIcon);

    return TargetIcon;

fail:
    /* Clean up scratch resources we created */
    if (NULL != OldTargetBitmap) SelectObject(TargetDC, OldTargetBitmap);
    if (NULL != TargetDC) DeleteDC(TargetDC);
    if (NULL != OldShortcutBitmap) SelectObject(ShortcutDC, OldShortcutBitmap);
    if (NULL != ShortcutDC) DeleteDC(ShortcutDC);
    if (NULL != TargetIconInfo.hbmColor) DeleteObject(TargetIconInfo.hbmColor);
    if (NULL != TargetIconInfo.hbmMask) DeleteObject(TargetIconInfo.hbmMask);
    if (NULL != ShortcutIconInfo.hbmColor) DeleteObject(ShortcutIconInfo.hbmColor);
    if (NULL != ShortcutIconInfo.hbmMask) DeleteObject(ShortcutIconInfo.hbmMask);
    if (NULL != ShortcutIcon) DestroyIcon(ShortcutIcon);

    return NULL;
}

/*****************************************************************************
 * SIC_GetIconIndex            [internal]
 *
 * Parameters
 *    sSourceFile    [IN]    filename of file containing the icon
 *    index        [IN]    index/resID (negated) in this file
 *
 * NOTES
 *  look in the cache for a proper icon. if not available the icon is taken
 *  from the file and cached
 */
EXTERN_C INT SIC_GetIconIndex(LPCWSTR pszFile, INT iIcon, UINT GilOut)
{
    HRESULT hr = SIC_FindOrAddIconFromLocation(pszFile, iIcon, GilOut);
    return SUCCEEDED(hr) ? hr : INVALID_INDEX;
}

/*****************************************************************************
 * SIC_Initialize            [internal]
 */
HRESULT SIC_Initialize(BOOL bFullInit)
{
    DWORD ilMask;
    BOOL success = FALSE;

    TRACE("Entered SIC_Initialize\n");

    if (SIC_IsInitialized())
        return S_OK;
    EnterCriticalSection(&SHELL32_SicCS);
    if (SIC_IsInitialized())
    {
        LeaveCriticalSection(&SHELL32_SicCS);
        return S_OK;
    }

    ZeroMemory(g_hSIL, sizeof(g_hSIL));
    HDPA hDPA = DPA_Create(16);
    if (!hDPA)
    {
        LeaveCriticalSection(&SHELL32_SicCS);
        return E_OUTOFMEMORY;
    }
    sic_hdpa = hDPA;
    g_InitState = -1;
    g_SIL_NoAssocIndex = INVALID_INDEX;
#if 1 // We handle SHIL_JUMBO correctly but it makes everything slower, disable for now.
    g_ListCount = 1 + (LOBYTE(GetVersion()) >= 6 ? SHIL_JUMBO : SHIL_SYSSMALL);
#else
    g_ListCount = 1 + SHIL_SYSSMALL;
#endif
    const UINT nMinPreloadIndex = SIID_FOLDEROPEN; // IShellIcon::GetIconOf
    UINT nPreloadIndex = bFullInit ? SIID_SLOWFILE : nMinPreloadIndex;
    UINT bpp = SIL_ReadIconMetrics(g_SILSizes);
    ASSERT(g_ListCount <= MAXSILCOUNT);
    ASSERT(SIL_GetIconSize(SHIL_LARGE) && SIL_GetIconSize(SHIL_SMALL));

    if (bpp <= 4)
        ilMask = ILC_COLOR4;
    else if (bpp <= 8)
        ilMask = ILC_COLOR8;
    else if (bpp <= 16)
        ilMask = ILC_COLOR16;
    else if (bpp <= 24)
        ilMask = ILC_COLOR24;
    else if (bpp <= 32)
        ilMask = ILC_COLOR32;
    else
        ilMask = ILC_COLOR;

    ilMask |= ILC_MASK;
    for (UINT i = 0; i < g_ListCount; ++i)
    {
        g_hSIL[i] = ImageList_Create(g_SILSizes[i], g_SILSizes[i], ilMask, 100, 100);
        TRACE("SIL#%d %d\n", i, g_SILSizes[i]);
        if (!g_hSIL[i])
        {
            ERR("Failed to create the %d icon list!\n", i);
            goto end;
        }
    }
    for (UINT i = 0; i <= nPreloadIndex; ++i)
    {
        const int ResId = MAKELONG(i, i + 1);
        ASSERT(i || (LOWORD(ResId) == SIID_DOCNOASSOC && HIWORD(ResId) == IDI_SHELL_DOCUMENT));
        // We don't check for failure here in case an icon is on a network drive that
        // has not been mapped yet (on startup).
        SIC_LoadOverlayIcon(i); // Not an overlay but we want the same shell32 redirection.
    }

    success = TRUE;
end:
    if (!success)
        SIC_Destroy();
    g_InitState = success ? 1 : 0;
    LeaveCriticalSection(&SHELL32_SicCS);
    return success ? S_OK : E_OUTOFMEMORY;
}

/*************************************************************************
 * SIC_Destroy
 *
 * frees the cache
 */
static INT CALLBACK sic_free(LPVOID ptr, LPVOID lparam)
{
    SIC_FreeEntry(ptr);
    return TRUE;
}

void SIC_Destroy(void)
{
    TRACE("\n");

    EnterCriticalSection(&SHELL32_SicCS);

    if (sic_hdpa)
        DPA_DestroyCallback(sic_hdpa, sic_free, NULL);
    sic_hdpa = NULL;
    for (UINT i = 0; i < g_ListCount; ++i)
    {
        ImageList_Destroy(g_hSIL[i]);
        g_hSIL[i] = NULL;
    }
    g_ListCount = 0;

    LeaveCriticalSection(&SHELL32_SicCS);
    //DeleteCriticalSection(&SHELL32_SicCS); //static
}

/*****************************************************************************
 * SIC_LoadOverlayIcon            [internal]
 *
 * Load a shell overlay icon and return its icon cache index.
 */
static int SIC_LoadOverlayIcon(int iIcon)
{
    WCHAR buffer[MAX_PATH * 2];
    PCWSTR pszFile = swShell32Name;
    if (FAILED(SIC_EnsureInitialized()))
        return INVALID_INDEX;
    if (HLM_GetIconW(iIcon, buffer, _countof(buffer), &iIcon))
        pszFile = buffer;

    return SIC_GetIconIndex(pszFile, iIcon, 0);
}

/*************************************************************************
 * Shell_GetImageLists            [SHELL32.71]
 *
 * PARAMETERS
 *  imglist[1|2] [OUT] pointer which receives imagelist handles
 *
 */
BOOL WINAPI Shell_GetImageLists(HIMAGELIST * lpBigList, HIMAGELIST * lpSmallList)
{
    TRACE("(%p,%p)\n",lpBigList,lpSmallList);

    if (FAILED(SIC_EnsureInitialized()))
        return FALSE;

    if (lpBigList)
        *lpBigList = g_hSIL[SHIL_LARGE];

    if (lpSmallList)
        *lpSmallList = g_hSIL[SHIL_SMALL];

    return TRUE;
}

static int SIC_GetDefaultOverridableCachedIndex(int index)
{
    PCWSTR path = g_pszShell32DotDll;
    WCHAR buf[MAX_PATH];
    if (HLM_GetIconW(index, buf, _countof(buf), &index))
        path = buf;
    return Shell_GetCachedImageIndexW(path, index, 0);
}

static inline int SIC_GetNoAssocCachedIndex()
{
    int index = g_SIL_NoAssocIndex;
    if (index < 0)
        index = g_SIL_NoAssocIndex = SIC_GetDefaultOverridableCachedIndex(SIID_DOCNOASSOC);
    return index;
}

static inline int SIC_GetDefaultIconCachedIndex(PCWSTR szIconPath, UINT GilOut)
{
    int index = INVALID_INDEX;
    if (GilOut & GIL_SIMULATEDOC)
        index = SIC_GetFallbackSimulateDocCachedIndex();
    else if ((GilOut & GIL_PERINSTANCE) && szIconPath && PathIsExeW(szIconPath))
        index = SIC_GetFallbackExeCachedIndex();
    return index >= 0 ? index : SIC_GetNoAssocCachedIndex();
}

EXTERN_C int SHELL_GetShell32IconLocation(UINT GilIn, PCWSTR Hint, PWSTR Output)
{
    int index = SIID_DOCNOASSOC, fallback = INVALID_INDEX;
    if (Hint == MAKEINTRESOURCEW('D')) // Directory
    {
        fallback = SIID_FOLDER;
        index = (GilIn & GIL_OPENICON) ? SIID_FOLDEROPEN : SIID_FOLDER;
    }
    else if (Hint == MAKEINTRESOURCEW('V')) // Volume
    {
        index = SIID_DRIVEFIXED;
    }
    else if (!IS_INTRESOURCE(Hint) && PathIsExeW(Hint))
    {
        index = SIID_APPLICATION;
    }

    if (!HLM_GetIconW(index, Output, MAX_PATH, &index))
    {
        if (fallback < 0 || !HLM_GetIconW(fallback, Output, MAX_PATH, &index))
            wcscpy(Output, swShell32Name);
    }
    return index;
}

static HRESULT SIC_IconIndexFromEI(IExtractIconW *pEIW, IExtractIconA *pEIA,
                                   UINT GilIn, int *pIndex)
{
    BOOL bCacheFallback = FALSE; // TODO, is this the fHandlerOk case?! or not?
    UINT GilOut = 0;
    int iList = INVALID_INDEX, iSource = INVALID_INDEX;
    WCHAR szIco[MAX_PATH];
    *szIco = UNICODE_NULL;
    CHAR szIcoA[MAX_PATH];
    HRESULT hr = GetIconLocationFromEI(pEIW, pEIA, GilIn, szIco,
                                       _countof(szIco), szIcoA, &iSource, &GilOut);

    if (SUCCEEDED(hr) && (GilOut & GIL_NOTFILENAME) && szIco[0] == '*' && !szIco[1] && iSource >= 0)
    {
        *pIndex = iSource; // EI::GetIconLocation returned the SIL index directly
        return hr;
    }

#if SICLINKOVERLAYHACK
    GilOut |= (GilIn & GIL_FORSHORTCUT); // SHGetFileInfo and CShellLink asking for the hack
#endif

    // If EI::GetIconLocation claims it returned a valid location, try getting it directly from the cache
    if (hr == S_OK && *szIco)
    {
#if !(SICLINKOVERLAYHACK)
        if (!(GilOut & GIL_NOTFILENAME) && SIC_IsShell32DllPath(szIco))
        {
            iList = Shell_GetCachedImageIndexW(g_pszShell32DotDll, iSource, GilOut);
        }
        else
#endif
             if (!(GilOut & GIL_DONTCACHE))
        {
            iList = SIC_TryLookupIconIndex(szIco, iSource, GilOut);
        }
    }

    // EI::GetIconLocation returns S_FALSE if it wants us to use a default icon.
    if (iList < 0 && hr != S_FALSE)
    {
        if (GilIn & GIL_ASYNC)
        {
            // The caller does not want us to hit the disk, check if their default icon is cached
            hr = GetIconLocationFromEI(pEIW, pEIA, GilIn | GIL_DEFAULTICON, szIco,
                                        _countof(szIco), szIcoA, &iSource, &GilOut);
            if (hr == S_OK && *szIco)
                iList = SIC_TryLookupIconIndex(szIco, iSource, GilOut);
            if (iList < 0)
                iList = SIC_GetDefaultIconCachedIndex(NULL, GilOut);
            *pIndex = iList;
            return E_PENDING;
        }
        iList = SIC_FindOrAddIcon(pEIW, pEIA, hr, szIco, szIcoA, iSource, GilOut);
    }

    if (iList < 0)
    {
        iList = SIC_GetDefaultIconCachedIndex(NULL, GilOut);

        if (bCacheFallback && iList >= 0 && !(GilOut & (GIL_NOTFILENAME | GIL_DONTCACHE)) && *szIco)
            SIC_SetLookupIconIndex(szIco, iSource, GilOut, iList);
    }

#if SICLINKOVERLAYHACK
    if ((GilOut & GIL_FORSHORTCUT) && iList == SIC_GetNoAssocCachedIndex())
    {
        PCWSTR pszSrc = SUCCEEDED(hr) ? szIco : NULL;
        iSource = SHELL_GetShell32IconLocation(0, pszSrc, szIco); // We want shell32 registry remapping
        iSource = SIC_FindOrAddIconFromLocation(szIco, iSource, GilOut);
        if (iSource >= 0)
            iList = iSource;
    }
#endif
    *pIndex = iList;
    return hr;
}

static INT64 SIC_IconIndexFromPidlInternal(IShellFolder *pSF, IShellIcon *pSI,
                                            LPCITEMIDLIST pidl, UINT GilIn, int *pIndex)
{
    HRESULT hr = S_OK;
    int iIndex = INVALID_INDEX, iTemp = INVALID_INDEX;
    CComPtr<IExtractIconW> pEIW;
    CComPtr<IExtractIconA> pEIA;

    SIC_Initialize(FALSE);
    if (pSI && pSI->GetIconOf(pidl, GilIn, &iTemp) == S_OK && iTemp >= 0)
        iIndex = iTemp;
    else if (SUCCEEDED(pSF->GetUIObjectOf(0, 1, &pidl, IID_NULL_PPV_ARG(IExtractIconW, &pEIW))) && pEIW)
        hr = SIC_IconIndexFromEI(pEIW, NULL, GilIn, &iIndex);
    else if (SUCCEEDED(hr = pSF->GetUIObjectOf(0, 1, &pidl, IID_NULL_PPV_ARG(IExtractIconA, &pEIA))))
        hr = pEIA ? SIC_IconIndexFromEI(NULL, pEIA, GilIn, &iIndex) : E_FAIL;

    // Pack the raw index and result together so all states can be detected.
    const UINT64 result = hr | ((UINT64)iIndex << 32);
    *pIndex = iIndex != INVALID_INDEX ? iIndex : SIC_GetNoAssocCachedIndex();
    return result;
}

EXTERN_C HRESULT SIC_IconIndexFromPidl(IShellFolder *pSF, IShellIcon *pSI,
                                       LPCITEMIDLIST pidl, UINT GilIn, int *pIndex)
{
    UINT64 ret = SIC_IconIndexFromPidlInternal(pSF, NULL, pidl, GilIn, pIndex);
    if (ret == (S_OK | ((UINT64)INVALID_INDEX << 32)))
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_SHELL, S_FALSE);
    return (UINT)ret;
}

/*************************************************************************
 * SHIconIndexFromPIDL              [SHELL32.873] (Vista+)
 */
EXTERN_C HRESULT WINAPI SHIconIndexFromPIDL(IShellFolder *pSF, LPCITEMIDLIST pidl, UINT GilIn, int *pIndex)
{
    return (UINT)SIC_IconIndexFromPidlInternal(pSF, NULL, pidl, GilIn, pIndex);
}

/*************************************************************************
 * SHMapPIDLToSystemImageListIndex    [SHELL32.77]
 *
 * PARAMETERS
 *    sh     [IN]
 *    pidl   [IN]
 *    pIndex [OUT][OPTIONAL]    SIC index for GIL_OPENICON
 *
 */
int WINAPI SHMapPIDLToSystemImageListIndex(
    IShellFolder *sh,
    LPCITEMIDLIST pidl,
    int *pOpenIndex)
{
    int Index;
    UINT GilIn = 0;

    TRACE("(SF=%p,pidl=%p,%p)\n",sh,pidl,pOpenIndex);
    pdump(pidl);

#if SICLINKOVERLAYHACK
    if (SHELL_IsShortcut(pidl))
        GilIn |= GIL_FORSHORTCUT;
#endif
    if (pOpenIndex)
        SIC_IconIndexFromPidlInternal(sh, NULL, pidl, GilIn | GIL_OPENICON, pOpenIndex);
    SIC_IconIndexFromPidlInternal(sh, NULL, pidl, GilIn, &Index);
    return Index;
}

/*************************************************************************
 * SHMapIDListToImageListIndexAsync  [SHELL32.148]
 */
EXTERN_C HRESULT WINAPI SHMapIDListToImageListIndexAsync(IShellTaskScheduler *pts, IShellFolder *psf,
                                                LPCITEMIDLIST pidl, UINT flags,
                                                PFNASYNCICONTASKBALLBACK pfn, void *pvData, void *pvHint,
                                                int *piIndex, int *piIndexSel)
{
    FIXME("(%p, %p, %p, 0x%08x, %p, %p, %p, %p, %p)\n",
            pts, psf, pidl, flags, pfn, pvData, pvHint, piIndex, piIndexSel);
    return E_FAIL;
}

/*************************************************************************
 * Shell_GetCachedImageIndex        [SHELL32.72]
 *
 */
INT WINAPI Shell_GetCachedImageIndexA(LPCSTR szPath, INT nIndex, UINT bSimulateDoc)
{
    INT ret, len;
    LPWSTR szTemp;

    len = MultiByteToWideChar( CP_ACP, 0, szPath, -1, NULL, 0 );
    szTemp = (LPWSTR)HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) );
    MultiByteToWideChar( CP_ACP, 0, szPath, -1, szTemp, len );

    ret = Shell_GetCachedImageIndexW(szTemp, nIndex, bSimulateDoc);

    HeapFree( GetProcessHeap(), 0, szTemp );

    return ret;
}

INT WINAPI Shell_GetCachedImageIndexW(LPCWSTR szPath, INT nIndex, UINT bSimulateDoc)
{
    return SIC_GetIconIndex(szPath, nIndex, bSimulateDoc);
}

EXTERN_C INT WINAPI Shell_GetCachedImageIndexAW(LPCVOID szPath, INT nIndex, BOOL bSimulateDoc)
{    if( SHELL_OsIsUnicode())
      return Shell_GetCachedImageIndexW((LPCWSTR)szPath, nIndex, bSimulateDoc);
    return Shell_GetCachedImageIndexA((LPCSTR)szPath, nIndex, bSimulateDoc);
}

EXTERN_C INT WINAPI Shell_GetCachedImageIndex(LPCWSTR szPath, INT nIndex, UINT bSimulateDoc)
{
    return Shell_GetCachedImageIndexAW(szPath, nIndex, bSimulateDoc);
}

/*************************************************************************
 * SHGetNoAssocIconIndex       [SHELL32.848] (Vista+)
 */
EXTERN_C UINT WINAPI SHGetNoAssocIconIndex()
{
    return SIC_GetNoAssocCachedIndex();
}

/*************************************************************************
 * ExtractIconExW            [SHELL32.@]
 * RETURNS
 *  0 no icon found (or the file is not valid)
 *  or number of icons extracted
 */
UINT WINAPI ExtractIconExW(LPCWSTR lpszFile, INT nIconIndex, HICON * phiconLarge, HICON * phiconSmall, UINT nIcons)
{
    UINT ret = 0;

    /* get entry point of undocumented function PrivateExtractIconExW() in user32 */
#if defined(__CYGWIN__) || defined (__MINGW32__) || defined(_MSC_VER)
    static UINT (WINAPI*PrivateExtractIconExW)(LPCWSTR,int,HICON*,HICON*,UINT) = NULL;

    if (!PrivateExtractIconExW) {
        HMODULE hUser32 = GetModuleHandleA("user32");
        PrivateExtractIconExW = (UINT(WINAPI*)(LPCWSTR,int,HICON*,HICON*,UINT)) GetProcAddress(hUser32, "PrivateExtractIconExW");

        if (!PrivateExtractIconExW)
        return ret;
    }
#endif

    TRACE("%s %i %p %p %i\n", debugstr_w(lpszFile), nIconIndex, phiconLarge, phiconSmall, nIcons);
    ret = PrivateExtractIconExW(lpszFile, nIconIndex, phiconLarge, phiconSmall, nIcons);

    /* PrivateExtractIconExW() may return -1 if the provided file is not a valid PE image file or the said
     * file couldn't be found. The behaviour is correct although ExtractIconExW() only returns the successfully
     * extracted icons from a file. In such scenario, simply return 0.
    */
    if (ret == 0xFFFFFFFF)
    {
        WARN("Invalid file or couldn't be found - %s\n", debugstr_w(lpszFile));
        ret = 0;
    }

    return ret;
}

/*************************************************************************
 * ExtractIconExA            [SHELL32.@]
 */
UINT WINAPI ExtractIconExA(LPCSTR lpszFile, INT nIconIndex, HICON * phiconLarge, HICON * phiconSmall, UINT nIcons)
{
    UINT ret = 0;
    INT len = MultiByteToWideChar(CP_ACP, 0, lpszFile, -1, NULL, 0);
    LPWSTR lpwstrFile = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));

    TRACE("%s %i %p %p %i\n", lpszFile, nIconIndex, phiconLarge, phiconSmall, nIcons);

    if (lpwstrFile)
    {
        MultiByteToWideChar(CP_ACP, 0, lpszFile, -1, lpwstrFile, len);
        ret = ExtractIconExW(lpwstrFile, nIconIndex, phiconLarge, phiconSmall, nIcons);
        HeapFree(GetProcessHeap(), 0, lpwstrFile);
    }
    return ret;
}

/*************************************************************************
 *                ExtractAssociatedIconA (SHELL32.@)
 *
 * Return icon for given file (either from file itself or from associated
 * executable) and patch parameters if needed.
 */
HICON WINAPI ExtractAssociatedIconA(HINSTANCE hInst, LPSTR lpIconPath, LPWORD lpiIcon)
{
    HICON hIcon = NULL;
    INT len = MultiByteToWideChar(CP_ACP, 0, lpIconPath, -1, NULL, 0);
    /* Note that we need to allocate MAX_PATH, since we are supposed to fill
     * the correct executable if there is no icon in lpIconPath directly.
     * lpIconPath itself is supposed to be large enough, so make sure lpIconPathW
     * is large enough too. Yes, I am puking too.
     */
    LPWSTR lpIconPathW = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, MAX_PATH * sizeof(WCHAR));

    TRACE("%p %s %p\n", hInst, debugstr_a(lpIconPath), lpiIcon);

    if (lpIconPathW)
    {
        MultiByteToWideChar(CP_ACP, 0, lpIconPath, -1, lpIconPathW, len);
        hIcon = ExtractAssociatedIconW(hInst, lpIconPathW, lpiIcon);
        WideCharToMultiByte(CP_ACP, 0, lpIconPathW, -1, lpIconPath, MAX_PATH , NULL, NULL);
        HeapFree(GetProcessHeap(), 0, lpIconPathW);
    }
    return hIcon;
}

/*************************************************************************
 *                ExtractAssociatedIconW (SHELL32.@)
 *
 * Return icon for given file (either from file itself or from associated
 * executable) and patch parameters if needed.
 */
HICON WINAPI ExtractAssociatedIconW(HINSTANCE hInst, LPWSTR lpIconPath, LPWORD lpiIcon)
{
    HICON hIcon = NULL;
    WORD wDummyIcon = 0;

    TRACE("%p %s %p\n", hInst, debugstr_w(lpIconPath), lpiIcon);

    if(lpiIcon == NULL)
        lpiIcon = &wDummyIcon;

    hIcon = ExtractIconW(hInst, lpIconPath, *lpiIcon);

    if( hIcon < (HICON)2 )
    { if( hIcon == (HICON)1 ) /* no icons found in given file */
      { WCHAR tempPath[MAX_PATH];
        HINSTANCE uRet = FindExecutableW(lpIconPath,NULL,tempPath);

        if( uRet > (HINSTANCE)32 && tempPath[0] )
        { wcscpy(lpIconPath,tempPath);
          hIcon = ExtractIconW(hInst, lpIconPath, *lpiIcon);
          if( hIcon > (HICON)2 )
            return hIcon;
        }
      }

      if( hIcon == (HICON)1 )
        *lpiIcon = 2;   /* MSDOS icon - we found .exe but no icons in it */
      else
        *lpiIcon = 6;   /* generic icon - found nothing */

      if (GetModuleFileNameW(hInst, lpIconPath, MAX_PATH))
        hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(*lpiIcon));
    }
    return hIcon;
}

/*************************************************************************
 *                ExtractAssociatedIconExW (SHELL32.@)
 *
 * Return icon for given file (either from file itself or from associated
 * executable) and patch parameters if needed.
 */
EXTERN_C HICON WINAPI ExtractAssociatedIconExW(HINSTANCE hInst, LPWSTR lpIconPath, LPWORD lpiIconIdx, LPWORD lpiIconId)
{
  FIXME("%p %s %p %p): stub\n", hInst, debugstr_w(lpIconPath), lpiIconIdx, lpiIconId);
  return 0;
}

/*************************************************************************
 *                ExtractAssociatedIconExA (SHELL32.@)
 *
 * Return icon for given file (either from file itself or from associated
 * executable) and patch parameters if needed.
 */
EXTERN_C HICON WINAPI ExtractAssociatedIconExA(HINSTANCE hInst, LPSTR lpIconPath, LPWORD lpiIconIdx, LPWORD lpiIconId)
{
  HICON ret;
  INT len = MultiByteToWideChar( CP_ACP, 0, lpIconPath, -1, NULL, 0 );
  LPWSTR lpwstrFile = (LPWSTR)HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) );

  TRACE("%p %s %p %p)\n", hInst, lpIconPath, lpiIconIdx, lpiIconId);

  MultiByteToWideChar( CP_ACP, 0, lpIconPath, -1, lpwstrFile, len );
  ret = ExtractAssociatedIconExW(hInst, lpwstrFile, lpiIconIdx, lpiIconId);
  HeapFree(GetProcessHeap(), 0, lpwstrFile);
  return ret;
}


/****************************************************************************
 * SHDefExtractIconW        [SHELL32.@]
 */
HRESULT WINAPI SHDefExtractIconW(LPCWSTR pszIconFile, int iIndex, UINT uFlags,
                                 HICON* phiconLarge, HICON* phiconSmall, UINT nIconSize)
{
    UINT ret;
    HICON hIcons[2] = {};
    WARN("%s %d 0x%08x %p %p %d, semi-stub\n", debugstr_w(pszIconFile), iIndex, uFlags, phiconLarge, phiconSmall, nIconSize);

    if (!nIconSize)
        nIconSize = MAKELONG(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CXSMICON));

    ret = PrivateExtractIconsW(pszIconFile, iIndex, nIconSize, nIconSize, hIcons, NULL, 2, LR_DEFAULTCOLOR);
    /* FIXME: deal with uFlags parameter which contains GIL_ flags */
    if (ret == 0xFFFFFFFF)
      return E_FAIL;
    if (ret > 0) {
      if (phiconLarge)
        *phiconLarge = hIcons[0];
      else
        DestroyIcon(hIcons[0]);
      if (phiconSmall)
        *phiconSmall = hIcons[1];
      else
        DestroyIcon(hIcons[1]);
      return S_OK;
    }
    return S_FALSE;
}

/****************************************************************************
 * SHDefExtractIconA        [SHELL32.@]
 */
HRESULT WINAPI SHDefExtractIconA(LPCSTR pszIconFile, int iIndex, UINT uFlags,
                                 HICON* phiconLarge, HICON* phiconSmall, UINT nIconSize)
{
  HRESULT ret;
  INT len = MultiByteToWideChar(CP_ACP, 0, pszIconFile, -1, NULL, 0);
  LPWSTR lpwstrFile = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));

  TRACE("%s %d 0x%08x %p %p %d\n", pszIconFile, iIndex, uFlags, phiconLarge, phiconSmall, nIconSize);

  MultiByteToWideChar(CP_ACP, 0, pszIconFile, -1, lpwstrFile, len);
  ret = SHDefExtractIconW(lpwstrFile, iIndex, uFlags, phiconLarge, phiconSmall, nIconSize);
  HeapFree(GetProcessHeap(), 0, lpwstrFile);
  return ret;
}

/****************************************************************************
 * SHGetIconOverlayIndexA    [SHELL32.@]
 *
 * Returns the index of the overlay icon in the system image list.
 */
EXTERN_C INT WINAPI SHGetIconOverlayIndexA(LPCSTR pszIconPath, INT iIconIndex)
{
  FIXME("%s, %d\n", debugstr_a(pszIconPath), iIconIndex);

  return -1;
}

/****************************************************************************
 * SHGetIconOverlayIndexW    [SHELL32.@]
 *
 * Returns the index of the overlay icon in the system image list.
 */
EXTERN_C INT WINAPI SHGetIconOverlayIndexW(LPCWSTR pszIconPath, INT iIconIndex)
{
  FIXME("%s, %d\n", debugstr_w(pszIconPath), iIconIndex);

  return -1;
}

#if DBG
char* SH32Dbg_AccessSIC(INT_PTR Op, INT_PTR Param1) // Only debughlp.cpp should call this!
{
    switch (Op)
    {
        case 0: EnterCriticalSection(&SHELL32_SicCS); return NULL;
        case 1: LeaveCriticalSection(&SHELL32_SicCS); return NULL;
        case 2: return (char*)sic_hdpa;
    }
    return 0;
}
#endif // DBG
