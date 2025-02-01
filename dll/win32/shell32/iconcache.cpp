/*
 * PROJECT:     ReactOS shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shell Icon Cache (SIC)
 * COPYRIGHT:   Copyright 1998, 1999 Juergen Schmied
 *              Copyright 2025 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

EXTERN_C DWORD WINAPI ImageList_GetFlags(HIMAGELIST himl);
EXTERN_C DWORD WINAPI ImageList_SetFlags(HIMAGELIST himl, DWORD flags); // FIXME

/********************** THE ICON CACHE ********************************/

#define INVALID_INDEX -1
#ifndef SHIL_JUMBO
#define SHIL_JUMBO 4
#endif
#define MAXIMAGELISTCOUNT (SHIL_JUMBO + 1)
#define GIL_DOCUMENTEDSICENTRYMASK (GIL_NOTFILENAME | GIL_SIMULATEDOC) // SHUpdateImage
#define GIL_SICENTRYMASK (GIL_DOCUMENTEDSICENTRYMASK | GIL_FORSHORTCUT) // FIXME: Remove GIL_FORSHORTCUT

typedef struct
{
    LPWSTR sSourceFile;    /* file (not path!) containing the icon */
    DWORD dwSourceIndex;    /* index within the file, if it is a resoure ID it will be negated */
    DWORD dwListIndex;    /* index within the iconlist */
    DWORD dwFlags;        /* GIL_* flags */
    DWORD dwAccessTime;
} SIC_ENTRY, * LPSIC_ENTRY;

static HDPA        sic_hdpa = 0;

INT ShellLargeIconSize, ShellSmallIconSize;
static BYTE g_SilCount = 0;
static HIMAGELIST g_hSil[MAXIMAGELISTCOUNT] = {};
static WORD g_SilSize[MAXIMAGELISTCOUNT];
static WORD g_SilBpp; // Bits Per Pixel
static int g_LnkOverlayIndex = -1;
static volatile LONG g_SilInitialized = 0;

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

static INT CALLBACK sic_free(LPVOID ptr, LPVOID lparam);

#define SIC_CalculateAccessTime() ( 0 )

static inline void SIC_Free(SIC_ENTRY &entry)
{
    HeapFree(GetProcessHeap(), 0, entry.sSourceFile);
    SHFree(&entry);
}

static inline BYTE GetImageListCount()
{
    return g_SilCount;
}

static inline void DestroyIcons(HICON hIcons[], SIZE_T Count)
{
    for (SIZE_T i = 0; i < Count; ++i)
    {
        if (hIcons[i] && (hIcons[i] != hIcons[0] || i == 0))
            DestroyIcon(hIcons[i]);
    }
}

static BYTE SIC_BppToFlag(UINT Bpp)
{
    if (Bpp >= 24)
        return ILC_COLOR32;
    else if (Bpp >= 16)
        return ILC_COLOR16;
    else if (Bpp >= 8)
        return ILC_COLOR8;
    else if (Bpp <= 4)
        return ILC_COLOR4;
    return ILC_COLOR;
}

// Load metric value from registry
static INT SIC_GetMetricsValue(_In_ PCWSTR pszValueName, _In_ INT nDefaultValue)
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

static INT SIC_GetIconBPP(VOID) // Bits Per Pixel
{
    INT nDefaultBPP = SHGetCurColorRes();
    INT nIconBPP = SIC_GetMetricsValue(L"Shell Icon BPP", nDefaultBPP);
    return (nIconBPP > 0) ? nIconBPP : nDefaultBPP;
}

static WORD SIC_GetMetrics(WORD *SizeArray)
{
    // Note: Calculate based on SM_CXICON because the user can change SM_CXSMICON in desk.cpl
    for (SIZE_T shil = 0; shil < MAXIMAGELISTCOUNT; ++shil)
    {
        int def, reg;
        switch (shil)
        {
            case SHIL_LARGE:
                reg = SIC_GetMetricsValue(L"Shell Icon Size", def = GetSystemMetrics(SM_CXICON));
                break;
            case SHIL_SMALL :
                reg = SIC_GetMetricsValue(L"Shell Small Icon Size", def = GetSystemMetrics(SM_CXICON) / 2);
                break;
            case SHIL_EXTRALARGE:
                reg = def = GetSystemMetrics(SM_CXICON) * 3 / 2; // 48 by default
                break;
            case SHIL_SYSSMALL:
                reg = def = GetSystemMetrics(SM_CXSMICON);
                break;
            case SHIL_JUMBO:
                reg = def = 256;
                break;
            DEFAULT_UNREACHABLE;
        }
        SizeArray[shil] = (WORD)(reg > 0 ? reg : def);
    }
    return (WORD)SIC_GetIconBPP();
}

static BOOL SHELL32_HasSilMetricsChanged(const WORD *SizeArray, const WORD Bpp)
{
    if (Bpp != g_SilBpp)
        return TRUE;
    for (SIZE_T shil = 0; shil < MAXIMAGELISTCOUNT; ++shil)
    {
        if (SizeArray[shil] != g_SilSize[shil] && SizeArray[shil])
            return TRUE;
    }
    return FALSE;
}

/*BOOL SHELL32_HasSilChanged(HIMAGELIST *hImages, SIZE_T Count)
{
    WORD sizes[MAXIMAGELISTCOUNT] = {};
    WORD bpp = 0;
    for (SIZE_T shil = 0; shil < GetImageListCount() && shil < Count; ++shil)
    {
        int w = 0, h = 0;
        if (!hImages[shil])
            continue;
        if (hImages[shil] != g_hSil[shil])
            return TRUE;
        if (!bpp)
            bpp = ImageList_GetFlags(hImages[shil]) & 0xFE;
        ImageList_GetIconSize(hImages[shil], &w, &h);
        sizes[shil] = w;
    }
    return SHELL32_HasSilMetricsChanged(sizes, bpp);
}*/

static BOOL SIC_EnsureInitialized()
{
    BOOL result = g_SilInitialized;
    if (!result)
    {
        EnterCriticalSection(&SHELL32_SicCS);
        result = sic_hdpa || SIC_Initialize(FALSE);
        LeaveCriticalSection(&SHELL32_SicCS);
    }
#if DBG
    if (result)
        ASSERT(sic_hdpa && g_hSil[SHIL_LARGE] && g_hSil[SHIL_SMALL]);
#endif
    return result;
}

static void SIC_ConfigureImageList(HIMAGELIST hImageList)
{
    ImageList_SetBkColor(hImageList, GetSysColor(COLOR_WINDOW));
}

void SHELL32_InvalidateShellImageLists()
{
    if (!SIC_EnsureInitialized())
        return;

    WORD Sizes[MAXIMAGELISTCOUNT], Bpp = SIC_GetMetrics(Sizes);
    if (!SHELL32_HasSilMetricsChanged(Sizes, Bpp))
        return;

    EnterCriticalSection(&SHELL32_SicCS);
    if (sic_hdpa)
    {
        DPA_DestroyCallback(sic_hdpa, sic_free, NULL);
        DPA_DeleteAllPtrs(sic_hdpa);
        // Note: Must not DPA_Destroy nor sic_hdpa = NULL
    }
    for (SIZE_T shil = 0; shil < GetImageListCount(); ++shil)
    {
        ImageList_RemoveAll(g_hSil[shil]);
        UINT flags = ImageList_GetFlags(g_hSil[shil]);
        ImageList_SetFlags(g_hSil[shil], (flags & ~0xFF) | ILC_MASK | SIC_BppToFlag(Bpp));
        ImageList_SetIconSize(g_hSil[shil], Sizes[shil], Sizes[shil]);
        SIC_ConfigureImageList(g_hSil[shil]);
    }
    LeaveCriticalSection(&SHELL32_SicCS);
}

void SHELL32_NotifyShellImageListsSysColorsChanged()
{
    for (SIZE_T shil = 0; shil < GetImageListCount(); ++shil)
        SIC_ConfigureImageList(g_hSil[shil]);
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

    DWORD f1 = e1->dwFlags & GIL_SICENTRYMASK, f2 = e2->dwFlags & GIL_SICENTRYMASK;
    if (f1 != f2)
        return f1 < f2 ? -1 : 1;

    return _wcsicmp(e1->sSourceFile, e2->sSourceFile);
}

/* declare SIC_LoadOverlayIcon() */
static int SIC_LoadOverlayIcon(int icon_idx);

/*****************************************************************************
 * SIC_OverlayShortcutImage            [internal]
 *
 * NOTES
 *  Creates a new icon as a copy of the passed-in icon, overlayed with a
 *  shortcut image.
 * FIXME: This should go to the ImageList implementation!
 */
static HICON SIC_OverlayShortcutImage(HICON SourceIcon, UINT SHIL)
{
    ICONINFO ShortcutIconInfo, TargetIconInfo;
    HICON ShortcutIcon = NULL, TargetIcon;
    BITMAP TargetBitmapInfo, ShortcutBitmapInfo;
    HDC ShortcutDC = NULL,
      TargetDC = NULL;
    HBITMAP OldShortcutBitmap = NULL,
      OldTargetBitmap = NULL;

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
    if (g_LnkOverlayIndex == -1)
        g_LnkOverlayIndex = SIC_LoadOverlayIcon(IDI_SHELL_SHORTCUT - 1);

    if (g_LnkOverlayIndex != -1)
        ShortcutIcon = ImageList_GetIcon(g_hSil[SHIL], g_LnkOverlayIndex, ILD_TRANSPARENT);

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

static HRESULT SHELL32_DefExtractIcon(LPCWSTR pszPath, int iIndex, UINT GilFlags, HICON hIcons[], UINT Sizes, UINT LrFlags)
{
    UINT count = HIWORD(Sizes) ? 2 : 1;
    /*if (GilFlags & GIL_SIMULATEDOC)
    {
        FIXME: TODO
    }*/
    int found = PrivateExtractIconsW(pszPath, iIndex, Sizes, Sizes, hIcons, NULL, count, LrFlags);
    return found > 0 ? S_OK : found == -1 ? E_FAIL : S_FALSE;
}

static HRESULT SIC_ExtractIcons(HICON hIcons[], SIZE_T Count, LPCWSTR pszPath, int iIndex, UINT Gil, UINT Lr)
{
    HRESULT hr = S_OK;
    for (SIZE_T i = 0; i < Count; ++i)
    {
        HRESULT hr2 = SHELL32_DefExtractIcon(pszPath, iIndex, Gil, &hIcons[i], g_SilSize[i], Lr);
        if (hr2 != S_OK)
        {
            if (i == 0)
                return E_FAIL;
            TRACE("Using SHIL_LARGE as fallback for %d (%s,%d)\n", i, debugstr_w(pszPath), iIndex);
            hr = S_FALSE;
        }
    }
    return hr;
}

static INT SIC_AddIconToImageLists(HICON hIcons[], SIZE_T Count)
{
    int index = INVALID_INDEX;
    for (SIZE_T i = 0; i < Count; ++i)
    {
        HICON hIco = hIcons[i];
        if (!hIco)
        {
            if (i == 0)
                break;
            hIco = hIcons[0];
        }
        int temp = ImageList_AddIcon(g_hSil[i], hIco);
        if (temp == INVALID_INDEX || temp != index)
        {
            if (i != 0)
            {
                for (SIZE_T j = 0; j < i; ++j)
                    ImageList_Remove(g_hSil[j], index);
                index = INVALID_INDEX;
                break;
            }
            index = temp;
        }
    }
    return index;
}

static inline LPCWSTR SIC_GetCacheFileName(LPCWSTR sSourceFile)
{
    LPCWSTR name = PathFindFileNameW(sSourceFile);
    return _wcsicmp(name, L"shell32.dll") ? sSourceFile : name; // Relative path for shell32.dll
}

static int SIC_GetIconListIndexHelper(LPCWSTR CacheFile, INT SourceIndex, UINT GilFlags)
{
    if (GilFlags & GIL_DONTCACHE)
        return INVALID_INDEX;
    SIC_ENTRY entry = { const_cast<LPWSTR>(CacheFile), (UINT)SourceIndex, 0, GilFlags & GIL_SICENTRYMASK, 0 };
    EnterCriticalSection(&SHELL32_SicCS);
    int dpai = DPA_Search(sic_hdpa, &entry, 0, SIC_CompareEntries, 0, DPAS_SORTED);
    int index = dpai >= 0 ? ((LPSIC_ENTRY)DPA_GetPtr(sic_hdpa, dpai))->dwListIndex : INVALID_INDEX;
    LeaveCriticalSection(&SHELL32_SicCS);
    return index;
}

static int SIC_GetCachedIconListIndex(LPCWSTR pszPath, INT SourceIndex, UINT GilFlags)
{
    if (!sic_hdpa)
        return INVALID_INDEX;
    LPCWSTR CacheName = SIC_GetCacheFileName(pszPath);
    return SIC_GetIconListIndexHelper(CacheName, SourceIndex, GilFlags);
}

static INT SIC_AppendIconToCache(HICON hIcons[], SIZE_T Count, LPCWSTR CacheName, int iIndex, UINT Gil, UINT Lr)
{
    int ret = INVALID_INDEX, dpai;
    LPSIC_ENTRY lpsice = (LPSIC_ENTRY)SHAlloc(sizeof(SIC_ENTRY));
    if (!lpsice)
        return ret;
    SIZE_T cch = wcslen(CacheName) + 1;
    lpsice->sSourceFile = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, cch * sizeof(WCHAR));
    if (!lpsice->sSourceFile)
    {
        SHFree(lpsice);
        return ret;
    }
    wcscpy(lpsice->sSourceFile, CacheName);
    lpsice->dwSourceIndex = iIndex;
    lpsice->dwFlags = Gil & GIL_SICENTRYMASK;
    lpsice->dwAccessTime = SIC_CalculateAccessTime();

    EnterCriticalSection(&SHELL32_SicCS);
    dpai = DPA_Search(sic_hdpa, lpsice, 0, SIC_CompareEntries, 0, DPAS_SORTED | DPAS_INSERTAFTER);
    dpai = DPA_InsertPtr(sic_hdpa, dpai, lpsice);
    if (dpai != DPA_ERR)
    {
        ret = lpsice->dwListIndex = SIC_AddIconToImageLists(hIcons, Count);
        if (ret == INVALID_INDEX)
            SIC_Free(*lpsice);
    }
    else
    {
        SIC_Free(*lpsice);
    }
    LeaveCriticalSection(&SHELL32_SicCS);
    return ret;
}

static INT SIC_FindOrAddIcon(LPCWSTR pszPath, INT SourceIndex, UINT GilFlags, UINT LrFlags)
{
    if (!SIC_EnsureInitialized())
    {
DbgPrint("FindOrAddIcon: SIC_EnsureInitialized failed\n");
        return INVALID_INDEX;
    }

    LPCWSTR CacheName = SIC_GetCacheFileName(pszPath);
    int index = SIC_GetIconListIndexHelper(CacheName, SourceIndex, GilFlags);
    if (index != INVALID_INDEX)
    {
//DbgPrint("FindOrAddIcon: found %ls,%d cacheindex=%d\n", CacheName,SourceIndex, index);
        return index;
    }

    HICON hIcons[MAXIMAGELISTCOUNT];
    SIZE_T count = GetImageListCount();
    HRESULT hr = S_FALSE;
    /*if ((CacheName[0] | 32) == L's' && !_wcsicmp(CacheName, L"shell32.dll"))
    {// This is not correct TODO XP only remaps the first X icons and only when building the full list in init?!
        int remapIndex = SourceIndex;
        WCHAR remapPath[MAX_PATH * 2];
        C_ASSERT(IDI_SHELL_DISCONN == 49); // Assuming 1 to 49 are all present
        if (remapIndex < 0 && remapIndex >= -IDI_SHELL_DISCONN)
            remapIndex = -remapIndex - 1;
        DbgPrint("%d -> %d\n",SourceIndex,remapIndex );
        if (HLM_GetIconW(remapIndex, remapPath, _countof(remapPath), &remapIndex))
            hr = SIC_ExtractIcons(hIcons, count, remapPath, remapIndex, GilFlags, LrFlags);
    }
    if (hr != S_OK)*/
        hr = SIC_ExtractIcons(hIcons, count, pszPath, SourceIndex, GilFlags, LrFlags);
//DbgPrint("FindOrAddIcon X hr=%#x silC=%d %ls,%d\n", hr, count, pszPath, SourceIndex);
    if (FAILED(hr))
        return index;
    index = SIC_AppendIconToCache(hIcons, count, CacheName, SourceIndex, GilFlags, LrFlags);
//DbgPrint("FindOrAddIcon A index=%d count=%d\n", index, count);
    if (!(LrFlags & LR_SHARED))
        DestroyIcons(hIcons, count);
    return index;
}

static INT SIC_AddInitialIcon(INT SourceIndex)
{
    UINT LrFlags = 0, GilFlags = 0, count = GetImageListCount();
    HICON hIcons[MAXIMAGELISTCOUNT];
    HRESULT hr = S_FALSE;
    LPCWSTR pszPath = swShell32Name, CacheName = L"shell32.dll";
    ASSERT(SIC_GetCachedIconListIndex(pszPath, SourceIndex, GilFlags) == INVALID_INDEX);
    int index = INVALID_INDEX, RemapIndex = SourceIndex;
    WCHAR RemapPath[MAX_PATH * 2];
    if (HLM_GetIconW(RemapIndex, RemapPath, _countof(RemapPath), &RemapIndex))
        hr = SIC_ExtractIcons(hIcons, count, RemapPath, RemapIndex, GilFlags, LrFlags);
    if (hr != S_OK)
        hr = SIC_ExtractIcons(hIcons, count, pszPath, SourceIndex, GilFlags, LrFlags |= LR_SHARED);
    if (FAILED(hr))
        return index;
    index = SIC_AppendIconToCache(hIcons, count, CacheName, SourceIndex, GilFlags, LrFlags);
    if (!(LrFlags & LR_SHARED))
        DestroyIcons(hIcons, count);
    return index;
}

/****************************************************************************
 * SIC_LoadIcon                [internal]
 */
static INT SIC_LoadIcon(LPCWSTR pszPath, INT iIndex, UINT GilFlags)
{
    const UINT LrFlags = LR_COPYFROMRESOURCE;
    if (GilFlags & GIL_FORSHORTCUT)
    {
        C_ASSERT(GIL_SICENTRYMASK & GIL_FORSHORTCUT); // TODO: Remove all this overlay code?
        if (!SIC_EnsureInitialized())
            return INVALID_INDEX;

        int index = SIC_GetCachedIconListIndex(pszPath, iIndex, GilFlags);
        if (index != INVALID_INDEX)
            return index;
        
        HICON hIcons[MAXIMAGELISTCOUNT] = {}, hTemp;
        UINT count = GetImageListCount(), failed = FALSE, shil;
        for (shil = 0; shil < count; ++shil)
        {
            UINT size = g_SilSize[shil];
            HRESULT hr = SHELL32_DefExtractIcon(pszPath, iIndex, GilFlags & ~GIL_FORSHORTCUT, &hIcons[shil], size, LrFlags);
            if (hr != S_OK || (hTemp = SIC_OverlayShortcutImage(hIcons[shil], shil)) == NULL)
            {
                failed = TRUE;
                break;
            }
            DestroyIcon(hIcons[shil]);
            hIcons[shil] = hTemp;
        }
        if (!failed)
            index = SIC_AddIconToImageLists(hIcons, count);
        if (!(LrFlags & LR_SHARED))
            DestroyIcons(hIcons, count);
        if (index != INVALID_INDEX)
            return index;
        WARN("Failed to create shortcut overlayed icons\n");
    }
    return SIC_FindOrAddIcon(pszPath, iIndex, GilFlags, LrFlags);
}

/*****************************************************************************
 * SIC_GetIconIndex            [internal]
 *
 * Parameters
 *    sSourceFile    [IN]    filename of file containing the icon
 *    dwSourceIndex  [IN]    index/resID (negated) in this file
 *    GilFlags       [IN]    GIL_* flags
 *
 * NOTES
 *  look in the cache for a proper icon. if not available the icon is taken
 *  from the file and cached
 */
INT SIC_GetIconIndex(LPCWSTR sSourceFile, INT dwSourceIndex, DWORD GilFlags)
{
    int index = SIC_GetCachedIconListIndex(sSourceFile, dwSourceIndex, GilFlags);
    return index != INVALID_INDEX ? index : SIC_LoadIcon(sSourceFile, dwSourceIndex, GilFlags);
}

/*****************************************************************************
 * SIC_Initialize            [internal]
 */
BOOL SIC_Initialize(BOOL bFullInit)
{
    TRACE("Entered SIC_Initialize\n");
    DWORD ilMask;
    BOOL result = FALSE;

    if (g_SilInitialized)
    {
raced:
        TRACE("Icon cache already initialized\n");
        return TRUE;
    }

    EnterCriticalSection(&SHELL32_SicCS);
    if (g_SilInitialized)
    {
        LeaveCriticalSection(&SHELL32_SicCS);
        goto raced;
    }
DbgPrint("SIC_Initialize CRIT begin bFullInit=%d\n", bFullInit);//Sleep(100);
    sic_hdpa = DPA_Create(16);
    if (!sic_hdpa)
    {
        TRACE("DPA failed\n");
        return FALSE;
    }

    g_SilCount = (LOBYTE(GetVersion()) >= 6 ? SHIL_JUMBO : SHIL_SYSSMALL) + 1;
    g_SilBpp = SIC_GetMetrics(g_SilSize);
    ShellLargeIconSize = g_SilSize[SHIL_LARGE];
    ShellSmallIconSize = g_SilSize[SHIL_SMALL];
    ilMask = ILC_MASK | SIC_BppToFlag(g_SilBpp);

    UINT first = 0, minlast = 4; // IShellIcon::GetIconOf
    UINT last = bFullInit ? 48 : minlast; // XP loads 0..48 in Explorer
    C_ASSERT(IDI_SHELL_DOCUMENT - 1 == 0 && -IDI_SHELL_DOCUMENT == -1);

    for (SIZE_T shil = 0; shil < GetImageListCount(); ++shil)
    {
        g_hSil[shil] = ImageList_Create(g_SilSize[shil], g_SilSize[shil], ilMask, 100, 100);
        if (!g_hSil[shil])
        {
DbgPrint("ImageList_Create failed for %d as %d,,%#x\n", shil, g_SilSize[shil], ilMask);
            ERR("Failed to create imagelist %d.\n", shil);
            goto end;
        }
        SIC_ConfigureImageList(g_hSil[shil]);
    }

DbgPrint("---precache begin\n");
loadmore:
    for (UINT i = first; i <= last; ++i)
    {
        if (SIC_AddInitialIcon(i) == INVALID_INDEX)
        {
            ERR("Failed to add %d icon to cache.\n", i);
            goto end;
        }
    }
    if (last == minlast)
    {
        first = IDI_SHELL_SHARE - 1, last = IDI_SHELL_FOLDER_WAIT - 1;
        goto loadmore;
    }
/*    for (SIZE_T shil = 0; shil < GetImageListCount(); ++shil)
    {
        for (UINT i = first; i <= last; ++i)
        {
            int id = i - 1;
DbgPrint("pre %ls,%d\n", swShell32Name, id);
            if (SIC_FindOrAddIcon(swShell32Name, id, 0, LrFlags) == INVALID_INDEX)
            {
DbgPrint("SIC_FindOrAddIcon failed for %d %#x\n", id, LrFlags);
                ERR("Failed to add %d icon to cache.\n", id);
                goto end;
            }
        }
    }
    if (!bFullInit && minlast)
    {
        // FIXME: Remove this when the overlay manager is implemented and can load these
        first = IDI_SHELL_SHARE, last = IDI_SHELL_FOLDER_WAIT; // Overlays
        minlast = 0;
        goto cachemore;
    }*/
#if 0
    SIC_FindOrAddIcon(swShell32Name, -IDI_SHELL_DOCUMENT, 0, 0); // A ROS specific optimization?
#endif

    // TODO: Initialize overlay manager here

DbgPrint("---precache end\n");
    result = TRUE;
end:
    if (result)
        InterlockedIncrement(&g_SilInitialized);
    else
        SIC_Destroy();

    DbgPrint("SIC_Initialize %d %p(%d) %p(%d)\n", g_SilCount, g_hSil[0], g_SilSize[0], g_hSil[1], g_SilSize[1]);
    LeaveCriticalSection(&SHELL32_SicCS);
    return result;
}

/*************************************************************************
 * SIC_Destroy
 *
 * frees the cache
 */
static INT CALLBACK sic_free(LPVOID ptr, LPVOID lparam)
{
    SIC_Free(*(LPSIC_ENTRY)ptr);
    return TRUE;
}

void SIC_Destroy(void)
{
    TRACE("\n");

    EnterCriticalSection(&SHELL32_SicCS);
    g_SilInitialized = 0;

    if (sic_hdpa)
    {
        DPA_DestroyCallback(sic_hdpa, sic_free, NULL);
        DPA_Destroy(sic_hdpa);
        sic_hdpa = NULL;
    }

    for (SIZE_T shil = 0; shil < MAXIMAGELISTCOUNT; ++shil)
    {
        if (g_hSil[shil])
            ImageList_Destroy(g_hSil[shil]);
        g_hSil[shil] = NULL;
    }
    g_SilCount = 0;

    LeaveCriticalSection(&SHELL32_SicCS);
    //DeleteCriticalSection(&SHELL32_SicCS); //static
}

/*****************************************************************************
 * SIC_LoadOverlayIcon            [internal]
 *
 * Load a shell overlay icon and return its icon cache index.
 */
static int SIC_LoadOverlayIcon(int icon_idx)
{
    WCHAR buffer[1024];
    LPWSTR iconPath;
    int iconIdx;

    iconPath = swShell32Name;    /* default: load icon from shell32.dll */
    iconIdx = icon_idx;

    if (HLM_GetIconW(icon_idx, buffer, _countof(buffer), &iconIdx))
    {
        iconPath = buffer;
    }
    else
    {
        WARN("Failed to load icon with index %d, using default one\n", icon_idx);
    }
    return SIC_EnsureInitialized() ? SIC_LoadIcon(iconPath, iconIdx, 0) : INVALID_INDEX;
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

    if (!SIC_EnsureInitialized())
        return FALSE;

    if (lpBigList)
        *lpBigList = g_hSil[SHIL_LARGE];
    if (lpSmallList)
        *lpSmallList = g_hSil[SHIL_SMALL];

    return TRUE;
}

EXTERN_C HIMAGELIST SHELL32_GetImageList(UINT SHIL)
{
    return SIC_EnsureInitialized() && SHIL < GetImageListCount() ? g_hSil[SHIL] : NULL;
}

/*************************************************************************
 * PidlToSicIndex            [INTERNAL]
 *
 * PARAMETERS
 *    sh    [IN]    IShellFolder
 *    pidl    [IN]
 *    bBigIcon [IN]
 *    uFlags    [IN]    GIL_*
 *    pIndex    [OUT]    index within the SIC
 *
 */
BOOL PidlToSicIndex (
    IShellFolder * sh,
    LPCITEMIDLIST pidl,
    BOOL bBigIcon,
    UINT uFlags,
    int * pIndex)
{
    CComPtr<IExtractIconW>        ei;
    WCHAR        szIconFile[MAX_PATH];    /* file containing the icon */
    INT        iSourceIndex;        /* index or resID(negated) in this file */
    BOOL        ret = FALSE;
    UINT        dwFlags = 0;
    int        iShortcutDefaultIndex = INVALID_INDEX;

    TRACE("sf=%p pidl=%p %s\n", sh, pidl, bBigIcon?"Big":"Small");

    if (!SIC_EnsureInitialized())
        return FALSE;

    if (SUCCEEDED (sh->GetUIObjectOf(0, 1, &pidl, IID_NULL_PPV_ARG(IExtractIconW, &ei))))
    {
      if (SUCCEEDED(ei->GetIconLocation(uFlags &~ GIL_FORSHORTCUT, szIconFile, MAX_PATH, &iSourceIndex, &dwFlags)))
      {
        *pIndex = SIC_GetIconIndex(szIconFile, iSourceIndex, uFlags);
        ret = TRUE;
      }
    }

    if (INVALID_INDEX == *pIndex)    /* default icon when failed */
    {
      if (0 == (uFlags & GIL_FORSHORTCUT))
      {
        *pIndex = 0;
      }
      else
      {
        if (INVALID_INDEX == iShortcutDefaultIndex)
        {
          iShortcutDefaultIndex = SIC_LoadIcon(swShell32Name, 0, GIL_FORSHORTCUT);
        }
        *pIndex = (INVALID_INDEX != iShortcutDefaultIndex ? iShortcutDefaultIndex : 0);
      }
    }

    return ret;

}

/*************************************************************************
 * SHMapPIDLToSystemImageListIndex    [SHELL32.77]
 *
 * PARAMETERS
 *    sh    [IN]        pointer to an instance of IShellFolder
 *    pidl    [IN]
 *    pIndex    [OUT][OPTIONAL]    SIC index for big icon
 *
 */
int WINAPI SHMapPIDLToSystemImageListIndex(
    IShellFolder *sh,
    LPCITEMIDLIST pidl,
    int *pIndex)
{
    int Index;
    UINT uGilFlags = 0;

    TRACE("(SF=%p,pidl=%p,%p)\n",sh,pidl,pIndex);
    pdump(pidl);

    if (SHELL_IsShortcut(pidl))
        uGilFlags |= GIL_FORSHORTCUT;

    if (pIndex)
        if (!PidlToSicIndex ( sh, pidl, 1, uGilFlags, pIndex))
            *pIndex = -1;

    if (!PidlToSicIndex ( sh, pidl, 0, uGilFlags, &Index))
        return -1;

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

    WARN("(%s,%08x,%08x) semi-stub.\n",debugstr_a(szPath), nIndex, bSimulateDoc);

    len = MultiByteToWideChar( CP_ACP, 0, szPath, -1, NULL, 0 );
    szTemp = (LPWSTR)HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) );
    MultiByteToWideChar( CP_ACP, 0, szPath, -1, szTemp, len );

    ret = SIC_GetIconIndex( szTemp, nIndex, 0 );

    HeapFree( GetProcessHeap(), 0, szTemp );

    return ret;
}

INT WINAPI Shell_GetCachedImageIndexW(LPCWSTR szPath, INT nIndex, UINT bSimulateDoc)
{
    WARN("(%s,%08x,%08x) semi-stub.\n",debugstr_w(szPath), nIndex, bSimulateDoc);

    return SIC_GetIconIndex(szPath, nIndex, 0);
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
    HICON hIcons[2];
    WARN("%s %d 0x%08x %p %p %d, semi-stub\n", debugstr_w(pszIconFile), iIndex, uFlags, phiconLarge, phiconSmall, nIconSize);

    if (!nIconSize)
    {
        WORD largesize = SIC_EnsureInitialized() ? g_SilSize[SHIL_LARGE] : GetSystemMetrics(SM_CXICON);
        WORD smallsize = g_SilCount >= SHIL_SYSSMALL ? g_SilSize[SHIL_SYSSMALL] : GetSystemMetrics(SM_CXSMICON);
        nIconSize = MAKELONG(largesize, smallsize);
    }
    HRESULT hr = SHELL32_DefExtractIcon(pszIconFile, iIndex, uFlags, hIcons, nIconSize, LR_DEFAULTCOLOR);
    if (hr == S_OK)
    {
        if (phiconLarge)
            *phiconLarge = hIcons[0];
        else
            DestroyIcon(hIcons[0]);
        if (phiconSmall)
            *phiconSmall = hIcons[1];
        else
            DestroyIcon(hIcons[1]);
    }
    return hr;
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
