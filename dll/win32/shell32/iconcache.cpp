/*
 * PROJECT:     ReactOS shell32
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shell Icon Cache (SIC)
 * COPYRIGHT:   Copyright 1998, 1999 Juergen Schmied
 *              Copyright 2025 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

#define RUNTIME_FIELD_OFFSET(t, f) ((LONG)(LONG_PTR)&(((t*) 0)->f)) // FIELD_OFFSET is broken on GCC
EXTERN_C BOOL PathIsExeW(LPCWSTR lpszPath);

/********************** THE ICON CACHE ********************************/

#define INVALID_INDEX -1
#define GILCACHEMASK (GIL_NOTFILENAME | GIL_SIMULATEDOC | GIL_SHIELD) // SHUpdateImage
#undef GILCACHEMASK
#define GILCACHEMASK (GIL_NOTFILENAME | GIL_SIMULATEDOC | GIL_SHIELD | GIL_FORSHORTCUT) // FIXME: Remove the GIL_FORSHORTCUT hack
PCWSTR g_pszShell32dotDll = L"shell32.dll"; // Shell32 is special and does not use the path in the cache

typedef struct
{
    LPCWSTR sSourceFile;
    DWORD dwSourceIndex;    /* index within the file, if it is a resoure ID it will be negated */
    DWORD dwListIndex;    /* index within the iconlist */
    DWORD dwFlags;        /* GIL_* flags */
    DWORD dwAccessTime;
    WCHAR szBuf[ANYSIZE_ARRAY];
} SIC_ENTRY, * LPSIC_ENTRY;

static HDPA        sic_hdpa = 0;

enum { LISTCOUNT = 2 }; // TODO: SHIL_EXTRALARGE etc
HIMAGELIST g_Lists[LISTCOUNT] = {};
UINT g_IconSizes[LISTCOUNT] = {}; // NOTE: Shell icon sizes are always square
UINT ShellIconBPP = 0; // Bits Per Pixel
INT8 g_CacheStockIconHasCustomIcon[SIID_RECYCLERFULL + 1];
int g_LnkOverlayIndex = INVALID_INDEX;

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

#define SIID_TO_SHELL32_ICONPATHINDEX(siid) ( siid ) // Negative resource index is faster but Windows uses positive index
static INT SH32_GetStockSysIconIndex(UINT SIID);
#define SIC_LoadOverlayIcon SH32_GetStockSysIconIndex

UINT SIC_GetIconSize(UINT SHIL)
{
    return SHIL < _countof(g_Lists) ? g_IconSizes[SHIL] : 0;
}

HIMAGELIST SIC_GetList(UINT SHIL)
{
    return SHIL < _countof(g_Lists) ? g_Lists[SHIL] : NULL;
}

// Load metric value from registry
static INT
SIC_GetMetricsValue(
    _In_ PCWSTR pszValueName)
{
    WCHAR szValue[64];
    DWORD cbValue = sizeof(szValue);
    DWORD error = SHGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics",
                              pszValueName, NULL, szValue, &cbValue);
    if (error)
        return 0;
    szValue[_countof(szValue) - 1] = UNICODE_NULL; // Avoid buffer overrun
    return _wtoi(szValue);
}

static INT SIC_GetLargeIconSize(VOID)
{
    INT nIconSize = SIC_GetMetricsValue(L"Shell Icon Size");
    return (nIconSize > 0) ? nIconSize : GetSystemMetrics(SM_CXICON);
}

static INT SIC_GetSmallIconSize(VOID)
{
    INT nIconSize = SIC_GetMetricsValue(L"Shell Small Icon Size");
    return (nIconSize > 0) ? nIconSize : GetSystemMetrics(SM_CXSMICON);
}

static INT SIC_GetIconBPP(VOID) // Bits Per Pixel
{
    INT nIconBPP = SIC_GetMetricsValue(L"Shell Icon BPP");
    return (nIconBPP > 0) ? nIconBPP : SHGetCurColorRes();
}

#define SHELL_DpiAwareScale(x) (x) // TODO

static void SIC_InitializeIconSizes(_Out_ UINT Sizes[])
{
    for (UINT i = 0; i < LISTCOUNT; ++i)
    {
        switch (i)
        {
            case SHIL_LARGE: Sizes[i] = SHELL_DpiAwareScale(SIC_GetLargeIconSize()); break;
            case SHIL_SMALL: Sizes[i] = SHELL_DpiAwareScale(SIC_GetSmallIconSize()); break;
            case SHIL_EXTRALARGE: Sizes[i] = SHELL_DpiAwareScale(GetSystemMetrics(SM_CXICON) * 3 / 2); break; // 48x48
            case SHIL_SYSSMALL: Sizes[i] = GetSystemMetrics(SM_CXSMICON); break;
            case SHIL_JUMBO: Sizes[i] = 256; break; // MSDN: 256 pixels regardless of the dpi-aware setting
        }
    }
}

static inline LPCWSTR SIC_GetPathForEntry(LPCWSTR pszFull, WCHAR &chHint)
{
    // Shell32.dll is special and stored without a path (for registry compatibility).
    // A path like "%windir%\explorer.exe" is stored directly (PrivateExtractIconsW will expand when extracting).
    LPCWSTR pszFileName = PathFindFileNameW(pszFull);
    chHint = towupper(*pszFileName);
    return !wcsicmp(pszFileName, g_pszShell32dotDll) ? g_pszShell32dotDll : pszFull;
}

static inline void SIC_FreeEntry(LPSIC_ENTRY p)
{
    LocalFree(p);
}

static void SIC_InitLookupEntry(SIC_ENTRY &sice, PCWSTR pszPath, int iIcon, UINT GilOut)
{
    WCHAR chHint;
    LPCWSTR pszFileName = SIC_GetPathForEntry(pszPath, chHint);
    sice.dwSourceIndex = iIcon;
    sice.dwFlags = MAKELONG(GilOut & GILCACHEMASK, chHint); // Hint to make comparison faster
    sice.sSourceFile = pszFileName;
}

static LPSIC_ENTRY SIC_AllocEntry(LPCWSTR pszPath, UINT IconIndex, UINT GilOut)
{
    SIC_ENTRY templ;
    SIC_InitLookupEntry(templ, pszPath, IconIndex, GilOut);
    const SIZE_T cch = wcslen(templ.sSourceFile) + 1;
    LPSIC_ENTRY p = (LPSIC_ENTRY)LocalAlloc(LMEM_FIXED, RUNTIME_FIELD_OFFSET(SIC_ENTRY, szBuf[cch]));
    if (!p)
        return p;
    *p = templ;
    p->dwAccessTime = 0;
    p->sSourceFile = p->szBuf;
    CopyMemory(p->szBuf, templ.sSourceFile, cch * sizeof(*pszPath));
    return p;
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

    ASSERT((LOWORD(e1->dwFlags | e2->dwFlags) & ~GILCACHEMASK) == 0);
    if (e1->dwFlags != e2->dwFlags)
        return (e1->dwFlags < e2->dwFlags) ? -1 : 1;

    return _wcsicmp(e1->sSourceFile,e2->sSourceFile);
}

static inline INT SIC_LockedLookupIconIndex(SIC_ENTRY &sice)
{
    INT i = DPA_Search(sic_hdpa, &sice, 0, SIC_CompareEntries, 0, DPAS_SORTED);
    return i == -1 ? INVALID_INDEX : ((SIC_ENTRY*)DPA_FastGetPtr(sic_hdpa, i))->dwListIndex;
}

static inline INT SIC_LockedGetInsertPos(SIC_ENTRY &sice)
{
    return DPA_Search(sic_hdpa, &sice, 0, SIC_CompareEntries, 0, DPAS_SORTED | DPAS_INSERTAFTER);
}

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

    int &s_imgListIdx = g_LnkOverlayIndex;
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
        s_imgListIdx = SIC_LoadOverlayIcon(IDI_SHELL_SHORTCUT - 1);

    if (s_imgListIdx != -1)
    {
        HIMAGELIST hList = SIC_GetList(SHIL);
        ShortcutIcon = ImageList_GetIcon(hList, s_imgListIdx, ILD_TRANSPARENT);
    } else
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

static INT SIC_LockedAddIconNoCacheCheck(LPSIC_ENTRY &pEntry, HICON hIcons[])
{
    INT index, lastindex;
    UINT failed, i;
    for (i = 0, failed = 0; SIC_GetIconSize(i); ++i)
    {
        index = ImageList_AddIcon(g_Lists[i], hIcons[i]);
        //ASSERT(i == 0 || index == lastindex); // All icons must have the same index in all imagelists
if (!hIcons[i])DbgPrint("can't add !HICON to IL %ls,%d\n", pEntry->sSourceFile, pEntry->dwSourceIndex);
if (!  (i == 0 || index == lastindex)   )Win32DbgPrint("",0,"ASSERT --- idx=%d %ls,%d,%#x %d=%p\n", index, pEntry->sSourceFile, pEntry->dwSourceIndex, pEntry->dwFlags, i, hIcons[i]);
        lastindex = index;
        if (index != -1)
            continue;
        failed = ++i;
        break;
    }

    if (!failed) // We have added the icons to all the lists, now insert the entry
    {
        pEntry->dwListIndex = index;
        if (DPA_InsertPtr(sic_hdpa, SIC_LockedGetInsertPos(*pEntry), pEntry) != -1)
        {
//Win32DbgPrint("", 0, "Add %d=%ls,%d,%#x\n", index, pEntry->sSourceFile, pEntry->dwSourceIndex, pEntry->dwFlags);
            pEntry = NULL; // We took owership
            return index;
        }
    }

    for (i = 0; i < failed; ++i)
        ImageList_Remove(g_Lists[i], index);
    return INVALID_INDEX;
}

static inline void SIC_OverlayShortcutHack(UINT Gil, HICON hIcons[], UINT Count)
{
    for (UINT i = 0; i < Count && (Gil & GIL_FORSHORTCUT); ++i) // FIXME: Remove this hack and use overlays
    {
        HICON hIco = SIC_OverlayShortcutImage(hIcons[i], i);
        if (!hIco)
            break; // Note: We silently eat failures
        DestroyIcon(hIcons[i]);
        hIcons[i] = hIco;
    }
}

/*static INT SIC_LockedAddIconNoCacheCheck(LPCWSTR pszSource, INT SourceIndex, UINT Gil)
{
    PCWSTR pszPath = pszSource == g_pszShell32dotDll ? swShell32Name : pszSource;
    HICON hIcons[LISTCOUNT], hIco;
    INT ret = INVALID_INDEX;
    UINT failed = 0, i, count;
    for (i = 0; SIC_GetIconSize(i);)
    {
        hIcons[i] = hIco = SH32_SHExtractIcon(pszPath, SourceIndex, g_IconSizes[i], g_IconSizes[i], LR_COPYFROMRESOURCE);
        count = ++i;
        if (hIco)
            continue;
        failed = count;
        break;
    }

    SIC_OverlayShortcutHack(Gil, hIcons, count);

    if (!failed)
    {
        LPSIC_ENTRY lpsice = SIC_AllocEntry(pszSource, SourceIndex, Gil);
        if (lpsice)
            ret = SIC_LockedAddIconNoCacheCheck(lpsice, hIcons);
        if (ret == INVALID_INDEX)
            SIC_FreeEntry(lpsice);
    }

    DestroyIcons(hIcons, count);
    return ret;
}*/

/*#if 0
static INT SIC_LockedFindOrAddIcon(SIC_ENTRY &entry)
{
    if (!entry.sSourceFile)
        return INVALID_INDEX;

    INT ret = SIC_LockedLookupIconIndex(entry);
    if (ret == INVALID_INDEX)
        ret = SIC_LockedAddIconNoCacheCheck(entry.sSourceFile, entry.dwSourceIndex, entry.dwFlags);
    return ret;
}

static INT SIC_FindOrAddIcon(LPCWSTR pszSource, INT SourceIndex, UINT GIL)
=======
        ret = SIC_LockedAppendIconNoCacheCheck(entry.sSourceFile, entry.dwSourceIndex, entry.dwFlags);
    return ret;
}


{
    SIC_ENTRY entry;
    SIC_InitLookupEntry(entry, pszSource, SourceIndex, GIL);

    EnterCriticalSection(&SHELL32_SicCS);
    INT ret = SIC_LockedFindOrAddIcon(entry);
    LeaveCriticalSection(&SHELL32_SicCS);
    return ret;
}
#endif*/

static INT SIC_FindOrAddIcon(HICON hIcons[], LPCWSTR pszSource, INT SourceIndex, UINT Gil)
{
    LPSIC_ENTRY lpsice = SIC_AllocEntry(pszSource, SourceIndex, Gil);
    if (!lpsice)
        return INVALID_INDEX;

    HICON hValidIcons[LISTCOUNT];
    HICON hValid = hIcons[0]; // FIXME: Don't just assume the first one is valid, scale (two rounds needed sometimes)
    for (UINT i = 0; SIC_GetIconSize(i); ++i)
    {
if (!hIcons[i]) DbgPrint("%d !HICON\n", i);
        hValidIcons[i] = hIcons[i] ? hIcons[i] : hValid;
    }

    SIC_OverlayShortcutHack(Gil, hValidIcons, LISTCOUNT);

    EnterCriticalSection(&SHELL32_SicCS);
    INT ret = INVALID_INDEX;
    if (!sic_hdpa && !SIC_Initialize())
        goto die;

    ret = SIC_LockedLookupIconIndex(*lpsice);
    if (ret == INVALID_INDEX)
        ret = SIC_LockedAddIconNoCacheCheck(lpsice, hValidIcons);
    LeaveCriticalSection(&SHELL32_SicCS);
die:
    if (ret == INVALID_INDEX)
        SIC_FreeEntry(lpsice);
    return ret;
}

static INT SIC_LookupIconIndex(PCWSTR FilePath, INT SourceIndex, UINT Gil)
{
    INT ret = INVALID_INDEX;
    SIC_ENTRY sice;
    SIC_InitLookupEntry(sice, FilePath, SourceIndex, Gil);

    EnterCriticalSection(&SHELL32_SicCS);
    if (sic_hdpa)
        ret = SIC_LockedLookupIconIndex(sice);
    LeaveCriticalSection(&SHELL32_SicCS);
    return ret;
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
INT SIC_GetIconIndex(LPCWSTR sSourceFile, INT dwSourceIndex, DWORD dwFlags)
{
    TRACE("%s %i\n", debugstr_w(sSourceFile), dwSourceIndex);

#if 01
    INT ret = SIC_LookupIconIndex(sSourceFile, dwSourceIndex, dwFlags);
    if (ret != INVALID_INDEX)
        return ret;


    PCWSTR pszPath = sSourceFile == g_pszShell32dotDll ? swShell32Name : sSourceFile;
    HICON hIcons[LISTCOUNT] = {};
    UINT any = 0;
    for (UINT i = 0, size; (size = SIC_GetIconSize(i)) != 0; ++i)
    {
        hIcons[i] = SH32_SHExtractIcon(pszPath, dwSourceIndex, size, size, LR_COPYFROMRESOURCE);
        any |= hIcons[i] != NULL;
if (!hIcons[i])DbgPrint("SIC_GetIconIndex failed to extract %ls,%d %dpx\n", pszPath, dwSourceIndex, size);
    }
    if (!any)
        return INVALID_INDEX;

    ret = SIC_FindOrAddIcon(hIcons, sSourceFile, dwSourceIndex, dwFlags);
    DestroyIcons(hIcons, _countof(hIcons));
    return ret;
#else
    INT ret = INVALID_INDEX;
    SIC_ENTRY sice;
    SIC_InitLookupEntry(sice, sSourceFile, dwSourceIndex, dwFlags);

    EnterCriticalSection(&SHELL32_SicCS);

    if (!sic_hdpa && !SIC_Initialize())
        goto die;

    ret = SIC_LockedLookupIconIndex(sice);
    if (ret == INVALID_INDEX)
        ret = SIC_LockedAddIconNoCacheCheck(sSourceFile, dwSourceIndex, dwFlags);
die:
    LeaveCriticalSection(&SHELL32_SicCS);
    return ret;
#endif
}

/*************************************************************************
 * SIC_Destroy
 *
 * frees the cache
 */
static INT CALLBACK SIC_FreeDpaEntry(LPVOID ptr, LPVOID lparam)
{
    SIC_FreeEntry((LPSIC_ENTRY)ptr);
    return TRUE;
}

static void SIC_Destroy(HDPA &hDpa, HIMAGELIST Lists[])
{
    if (hDpa)
        DPA_DestroyCallback(hDpa, SIC_FreeDpaEntry, NULL);
    hDpa = NULL;
    for (UINT i = 0; i < LISTCOUNT; ++i)
    {
        if (Lists[i])
            ImageList_Destroy(Lists[i]);
        Lists[i] = NULL;
    }
}

void SIC_Destroy(void)
{
    TRACE("\n");

    EnterCriticalSection(&SHELL32_SicCS);
    SIC_Destroy(sic_hdpa, g_Lists);
    LeaveCriticalSection(&SHELL32_SicCS);
    //DeleteCriticalSection(&SHELL32_SicCS); //static
}

static void SIC_InvalidateIndexCache()
{
    ZeroMemory(g_CacheStockIconHasCustomIcon, sizeof(g_CacheStockIconHasCustomIcon));
    g_LnkOverlayIndex = INVALID_INDEX;
}

/*****************************************************************************
 * SIC_Initialize            [internal]
 */
BOOL SIC_Initialize(void)
{
    DWORD ilMask, i;
    BOOL failed = FALSE, notify = FALSE;DbgPrint("SIC_Initialize ENTER ---- %p\n", sic_hdpa);

    TRACE("Entered SIC_Initialize\n");

    HIMAGELIST hLists[LISTCOUNT] = {}, hOldLists[LISTCOUNT];
    UINT bpp = SIC_GetIconBPP(), oldbpp = bpp; // Bits Per Pixel
    UINT sizes[LISTCOUNT], OldSizes[LISTCOUNT];
    SIC_InitializeIconSizes(sizes);

    if (sic_hdpa)
    {
        oldbpp = ShellIconBPP;
        BOOL changed = oldbpp != bpp;
        for (i = 0; i < LISTCOUNT; ++i)
            changed |= sizes[i] != SIC_GetIconSize(i);

        if (!changed)
        {
            TRACE("Icon cache already initialized\n");
            return TRUE;
        }
    }

    HDPA hDpa = DPA_Create(16);
    if (!hDpa)
    {
        ERR("Failed to create HDPA\n");
        return sic_hdpa != NULL;
    }

    EnterCriticalSection(&SHELL32_SicCS);
    HDPA hDpaOld = sic_hdpa;
    SIC_InvalidateIndexCache();

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

    ShellIconBPP = bpp;
    COLORREF WindowColor = GetSysColor(COLOR_WINDOW);
    for (i = 0; i < LISTCOUNT; ++i)
    {
        hOldLists[i] = g_Lists[i];
        OldSizes[i] = g_IconSizes[i];
        g_Lists[i] = hLists[i] = ImageList_Create(sizes[i], sizes[i], ilMask | ILC_SYSTEM, 100, 100);
        if (g_Lists[i])
            ImageList_SetBkColor(g_Lists[i], WindowColor);
        else
            failed = TRUE;
        g_IconSizes[i] = sizes[i];
    }

    if (!failed)
    {
        sic_hdpa = hDpa;
        // Note: This icon extraction depends on SH32_GetStockSysIconIndex to redirect via the "Shell Icons" key.
        const UINT lastloadindex = IDI_SHELL_DISCONN - 1; // Minimum SIID_FOLDEROPEN but Windows seems to add more
        // Initialize basic icons
        for (i = SIID_DOCNOASSOC; i <= lastloadindex; ++i)
            failed |= SH32_GetStockSysIconIndex(i) == INVALID_INDEX;
        // Initialize overlay icons
        for (i = SIID_SHARE; i <= SIID_SLOWFILE && i > lastloadindex; ++i)
            failed |= SH32_GetStockSysIconIndex(i) == INVALID_INDEX;
    }
    else
    {
        ERR("Failed to create HIMAGELISTs\n");
    }

    if (failed)
    {
        for (i = 0; hDpaOld && i < LISTCOUNT; ++i)
        {
            g_Lists[i] = hOldLists[i];
            g_IconSizes[i] = OldSizes[i];
        }
        ShellIconBPP = oldbpp;
        sic_hdpa = hDpaOld;
        SIC_Destroy(hDpa, hLists);
    }
    else if (hDpaOld)
    {
        notify = TRUE;
        SIC_Destroy(hDpaOld, hOldLists);
    }

    TRACE("hIconSmall=%p hIconBig=%p\n", hLists[SHIL_SMALL], hLists[SHIL_LARGE]);
    LeaveCriticalSection(&SHELL32_SicCS);

    if (notify)
        SHChangeNotify(SHCNE_UPDATEIMAGE, SHCNF_DWORD, (LPCVOID)(INT_PTR)-1, NULL); // Tell every IShellView to refresh
    return !failed;
}

void SIC_Notify(ULONG SHCNE)
{
    if (SHCNE == SHCNE_ASSOCCHANGED)
    {
        SIC_InvalidateIndexCache();
    }
    if (SHCNE == SHCNE_UPDATEIMAGE) // Only the desktop browser sends us this
    {
        HWND hTrayWnd = FindWindowW(L"Shell_TrayWnd", NULL);
        SendMessageW(hTrayWnd, WM_SETTINGCHANGE, 0, (LPARAM)L"TraySettings"); // A hacky way to refresh the start menu
    }
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

    if (!sic_hdpa && !SIC_Initialize())
        return FALSE;

    if (lpBigList)
        *lpBigList = SIC_GetList(SHIL_LARGE);

    if (lpSmallList)
        *lpSmallList = SIC_GetList(SHIL_SMALL);

    return TRUE;
}

static INT SIC_GetFallbackIconIndex(_In_ UINT GilOut, _In_opt_ PCWSTR pszPath)
{
    UINT siid = SIID_DOCNOASSOC;
    if (GilOut & GIL_SIMULATEDOC)
        siid = SIID_DOCASSOC;
    else if ((GilOut & GIL_PERINSTANCE) && pszPath && PathIsExeW(pszPath))
        siid = SIID_APPLICATION;

    int index = SH32_GetStockSysIconIndex(siid);
    if (index != INVALID_INDEX || siid == SIID_DOCNOASSOC)
        return index;
    return SH32_GetStockSysIconIndex(SIID_DOCNOASSOC);
}

static HRESULT SIC_DefExtractIcon(PCWSTR pszIconFile, int IconIndex, UINT Gil, HICON *phLarge, HICON *phSmall, UINT Sizes)
{
    // FIXME: return SHDefExtractIconW(szW, iIcon, Gil, phLarge, phSmall, Sizes);
    // SHDefExtractIconW (PrivateExtractIconsW) does not correctly extract two sizes from .ico files!
    // This bug is ROS specific and we can change to a single call when that is fixed.
    // It even returns a non-NULL invalid handle value, be careful!

    // TODO: GIL_SIMULATEDOC
    if (phLarge)
    {
        *phLarge = SH32_SHExtractIcon(pszIconFile, IconIndex, LOWORD(Sizes), LOWORD(Sizes), 0);
if (!*phLarge)Win32DbgPrint("",0,"SHEX!L %ls,%d %dpx\n", pszIconFile, IconIndex, LOWORD(Sizes));
    }
    if (phSmall)
    {
        *phSmall = SH32_SHExtractIcon(pszIconFile, IconIndex, HIWORD(Sizes), HIWORD(Sizes), 0);
if (!*phSmall)Win32DbgPrint("",0,"SHEX!S %ls,%d %dpx\n", pszIconFile, IconIndex, HIWORD(Sizes));
    }
    if ((!phLarge || *phLarge) && (!phSmall || *phSmall))
        return S_OK;
    DestroyIcons(phLarge, !!phLarge);
    DestroyIcons(phSmall, !!phSmall);
    return E_FAIL;
}

static HRESULT SIC_GetExtractedIcon(_In_opt_ IExtractIconW *pW, _In_opt_ IExtractIconA *pA, _In_ UINT GilIn, _Out_ int *pIndex)
{
    WCHAR wbuf[MAX_PATH];
    CHAR abuf[MAX_PATH];
    UINT GilOut = 0, GilInHack = GilIn & ~GIL_FORSHORTCUT;
    HRESULT hr;

    if (pW)
    {
        hr = pW->GetIconLocation(GilInHack, wbuf, _countof(wbuf), pIndex, &GilOut);
    }
    else
    {
        hr = pA->GetIconLocation(GilInHack, abuf, _countof(abuf), pIndex, &GilOut);
        if (SUCCEEDED(hr) && !SHAnsiToUnicode(abuf, wbuf, _countof(wbuf)))
            return E_FAIL;
    }
    GilOut |= (GilIn & GIL_FORSHORTCUT); // FIXME: Remove shortcut hack

    if (wbuf[0] == L'*' && !wbuf[1] && (GilOut & GIL_NOTFILENAME) && SUCCEEDED(hr)) // IExtractIcon added the icon
        return hr;

    if (hr == S_OK && !(GilOut & GIL_DONTCACHE) && *wbuf)
    {
        int ListIndex = SIC_LookupIconIndex(wbuf, *pIndex, GilOut); // Do we already have this icon?
        if (ListIndex != INVALID_INDEX)
        {
            *pIndex = ListIndex;
            return hr;
        }
    }

    if (hr == S_OK) // S_FALSE means use a default icon
    {
        UINT any = 0;
        HICON hIcons[LISTCOUNT] = {};
        for (UINT i = 0; SIC_GetIconSize(i); i += 2)
        {
            UINT Sizes = SIC_GetIconSize(i), NextSize = SIC_GetIconSize(i + 1);
            if (NextSize)
                Sizes = MAKELONG(Sizes, NextSize);

            if (pW)
                hr = pW->Extract(wbuf, *pIndex, &hIcons[i + 0], HIWORD(Sizes) ? &hIcons[i + 1] : NULL, Sizes);
            else
                hr = pA->Extract(abuf, *pIndex, &hIcons[i + 0], HIWORD(Sizes) ? &hIcons[i + 1] : NULL, Sizes);

            if (hr == S_FALSE && !(GilOut & GIL_NOTFILENAME))
            {
                hr = SIC_DefExtractIcon(wbuf, *pIndex, GilOut, &hIcons[i + 0], HIWORD(Sizes) ? &hIcons[i + 1] : NULL, Sizes);
            }

//Win32DbgPrint("",0,"E%d %#x %p,%p for %ls,%d\n", NextSize ? 2 : 1, hr, hIcons[i + 0], hIcons[i + 1], wbuf, *pIndex);
if (!&hIcons[i + 0])DbgPrint("SIC_GetExtractedIcon 1 failed %ls,%d\n", wbuf, *pIndex);
if (NextSize && !&hIcons[i + 1])DbgPrint("SIC_GetExtractedIcon 2 failed %ls,%d\n", wbuf, *pIndex);
            any |= SUCCEEDED(hr);
        }

        if (any)
        {
            *pIndex = SIC_FindOrAddIcon(hIcons, wbuf, *pIndex, GilOut);
            DestroyIcons(hIcons, _countof(hIcons));
            if (*pIndex != INVALID_INDEX)
                return hr;
        }
    }

    if (GilIn & GIL_FORSHORTCUT) // FIXME: Remove shortcut hack
    {
DbgPrint("GIL_FORSHORTCUT HACK #1\n");
        *pIndex = Shell_GetCachedImageIndexW(g_pszShell32dotDll, SIID_TO_SHELL32_ICONPATHINDEX(SIID_DOCNOASSOC), GIL_FORSHORTCUT);
        if (*pIndex != INVALID_INDEX)
            return S_FALSE;
    }
    *pIndex = SIC_GetFallbackIconIndex(GilOut, wbuf);
    return *pIndex != INVALID_INDEX ? S_FALSE : E_FAIL;
}

static int SIC_GetFolderItemIcon(_In_ IShellFolder *pSF, _In_ LPCITEMIDLIST pidl, _In_ UINT GilIn)
{
    int ret;
    if (!sic_hdpa && !SIC_Initialize())
        return INVALID_INDEX;

    CComPtr<IExtractIconW> pEIW;
    if (SUCCEEDED(pSF->GetUIObjectOf(0, 1, &pidl, IID_NULL_PPV_ARG(IExtractIconW, &pEIW))) && pEIW)
    {
        HRESULT hr = SIC_GetExtractedIcon(pEIW, NULL, GilIn | GIL_FORSHELL, &ret);
        return SUCCEEDED(hr) ? ret : INVALID_INDEX;
    }
    CComPtr<IExtractIconA> pEIA;
    if (SUCCEEDED(pSF->GetUIObjectOf(0, 1, &pidl, IID_NULL_PPV_ARG(IExtractIconA, &pEIA))) && pEIA)
    {
        HRESULT hr = SIC_GetExtractedIcon(NULL, pEIA, GilIn | GIL_FORSHELL, &ret);
        return SUCCEEDED(hr) ? ret : INVALID_INDEX;
    }
    return INVALID_INDEX;
}

/*************************************************************************
 * PidlToSicIndex            [INTERNAL]
 *
 * PARAMETERS
 *    sh    [IN]    IShellFolder
 *    pidl    [IN]
 *    GilIn    [IN]    GIL_*
 *    pIndex    [OUT]    index within the SIC
 *
 */
BOOL PidlToSicIndex (
    IShellFolder * sh,
    LPCITEMIDLIST pidl,
    UINT GilIn,
    int * pIndex)
{
    int idx = SIC_GetFolderItemIcon(sh, pidl, GilIn);
    *pIndex = idx != INVALID_INDEX ? idx : 0;
    return idx != INVALID_INDEX;
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
    UINT uGilFlags = 0;

    TRACE("(SF=%p,pidl=%p,%p)\n",sh,pidl,pIndex);
    pdump(pidl);

    if (SHELL_IsShortcut(pidl))
        uGilFlags |= GIL_FORSHORTCUT; // FIXME: Remove hack

    if (pIndex)
        *pIndex = SIC_GetFolderItemIcon(sh, pidl, uGilFlags | GIL_OPENICON);
    return SIC_GetFolderItemIcon(sh, pidl, uGilFlags);
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
 * SHLookupIconIndexW        [SHELL32.8]
 *
 */
EXTERN_C INT WINAPI SHLookupIconIndexW(LPCWSTR lpName, INT iIndex, UINT Gil)
{
    return SIC_LookupIconIndex(lpName, iIndex, Gil);
}

/*************************************************************************
 * SHLookupIconIndexA        [SHELL32.7]
 *
 */
EXTERN_C INT WINAPI SHLookupIconIndexA(LPCSTR lpName, INT iIndex, UINT Gil)
{
    WCHAR buf[MAX_PATH];
    if (!SHAnsiToUnicode(lpName, buf, _countof(buf)))
        return -1;
    return SHLookupIconIndexW(buf, iIndex, Gil);
}

/*************************************************************************
 * Shell_GetCachedImageIndex        [SHELL32.72]
 *
 */
EXTERN_C INT WINAPI Shell_GetCachedImageIndexA(LPCSTR szPath, INT nIndex, UINT uIconFlags)
{
    INT ret, len;
    LPWSTR szTemp;

    TRACE("(%s,%08x,%08x)\n",debugstr_a(szPath), nIndex, uIconFlags);

    len = MultiByteToWideChar( CP_ACP, 0, szPath, -1, NULL, 0 );
    szTemp = (LPWSTR)HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) );
    MultiByteToWideChar( CP_ACP, 0, szPath, -1, szTemp, len );
    ret = szTemp ? Shell_GetCachedImageIndexW(szTemp, nIndex, uIconFlags) : -1;
    HeapFree( GetProcessHeap(), 0, szTemp );
    return ret;
}

EXTERN_C INT WINAPI Shell_GetCachedImageIndexW(LPCWSTR szPath, INT nIndex, UINT uIconFlags)
{
    TRACE("(%s,%08x,%08x)\n",debugstr_w(szPath), nIndex, uIconFlags);

    return SIC_GetIconIndex(szPath, nIndex, uIconFlags);
}

EXTERN_C INT WINAPI Shell_GetCachedImageIndexAW(LPCVOID szPath, INT nIndex, UINT uIconFlags)
{
    if( SHELL_OsIsUnicode())
        return Shell_GetCachedImageIndexW((LPCWSTR)szPath, nIndex, uIconFlags);
    return Shell_GetCachedImageIndexA((LPCSTR)szPath, nIndex, uIconFlags);
}

EXTERN_C INT WINAPI Shell_GetCachedImageIndex(LPCWSTR szPath, INT nIndex, UINT uIconFlags)
{
    return Shell_GetCachedImageIndexAW(szPath, nIndex, uIconFlags);
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
    HICON hIcons[2];
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

/****************************************************************************
 * SHIconIndexFromPIDL    [SHELL32.873]
 *
 */
EXTERN_C HRESULT WINAPI SHIconIndexFromPIDL(void *unk1, void *unk2, void *unk3, void *unk4)
{
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HICON SH32_LoadStockIcon(UINT SIID, UINT Size, UINT LrFlags)
{
    WCHAR szIconPath[MAX_PATH];
    int Index;
    if (SIID < _countof(g_CacheStockIconHasCustomIcon) && g_CacheStockIconHasCustomIcon[SIID] >= 0)
    {
        if (HLM_GetIconW(SIID, szIconPath, _countof(szIconPath), &Index))
        {
            HICON hIcon;
            if (SHDefExtractIconW(szIconPath, Index, 0, &hIcon, NULL, Size) == S_OK)
                return hIcon;
        }
        g_CacheStockIconHasCustomIcon[SIID] = -1; // Not customized in the registry, stop looking there
    }
    if (!IsValidStockIconIdForShell32Icon(SIID))
        return NULL;
    return (HICON)LoadImageW(shell32_hInstance, MAKEINTRESOURCEW(SIID + 1), IMAGE_ICON, Size, Size, LrFlags);
}

static INT SH32_GetStockSysIconIndex(UINT SIID)
{
    WCHAR szIconPath[MAX_PATH];
    int Index;
    if (SIID < _countof(g_CacheStockIconHasCustomIcon) && g_CacheStockIconHasCustomIcon[SIID] >= 0)
    {
        if (g_CacheStockIconHasCustomIcon[SIID] > 0)
            return g_CacheStockIconHasCustomIcon[SIID];

        if (HLM_GetIconW(SIID, szIconPath, _countof(szIconPath), &Index))
        {
            Index = Shell_GetCachedImageIndexW(szIconPath, Index, 0);
            if (Index != INVALID_INDEX)
            {
                if (Index > 0 && Index <= SCHAR_MAX)
                    g_CacheStockIconHasCustomIcon[SIID] = (UINT8) Index;
                return Index;
            }
        }
        g_CacheStockIconHasCustomIcon[SIID] = -1; // Not customized in the registry, stop looking there
    }
    if (!IsValidStockIconIdForShell32Icon(SIID))
        return INVALID_INDEX;
    return Shell_GetCachedImageIndexW(g_pszShell32dotDll, SIID_TO_SHELL32_ICONPATHINDEX(SIID), 0);
}

/****************************************************************************
 * SHGetNoAssocIconIndex    [SHELL32.848]
 *
 */
EXTERN_C INT WINAPI SHGetNoAssocIconIndex()
{
    return SH32_GetStockSysIconIndex(SIID_DOCNOASSOC);
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
