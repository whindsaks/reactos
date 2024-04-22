/*
 * PROJECT:     ReactOS shell32
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     IAssociationArray and related functions
 * COPYRIGHT:   Whindmar Saksit (whindsaks@proton.me)
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

/*
JIRA+: if (!RegLoadMUIStringW(hkey, L"FriendlyTypeName", szFileType, len, NULL, 0, NULL))
JIRA+: SHCreate simple use ILCreateFromPath
JIRA+: SHMapPidlToSystem is not supposed to add lnk overlay?
*/



static HRESULT SHELL32_AssocGetFriendlyDocNameFromDotExt(LPCWSTR Ext, LPWSTR Buffer, SIZE_T cch)
{
    WCHAR progid[MAX_PATH];
    HKEY hKey, hProgidKey;
    DWORD err, errprogid, cb;
    err = RegOpenKeyExW(HKEY_CLASSES_ROOT, Ext, 0, KEY_READ, &hKey);
    if (err)
        return HRESULT_FROM_WIN32(err);
    cb = sizeof(progid);
    err = errprogid = RegGetValueW(hKey, NULL, NULL, RRF_RT_REG_SZ, NULL, progid, &cb);
    if (!errprogid)
    {
        err = ERROR_NOT_SUPPORTED;
        // Only map to progid if actually an extension
        if (*Ext == L'.' && RegOpenKeyExW(HKEY_CLASSES_ROOT, progid, 0, KEY_READ, &hProgidKey) == NO_ERROR)
        {
            err = RegLoadMUIStringW(hProgidKey, L"FriendlyTypeName", Buffer, cch, NULL, 0, NULL);
            if (err)
                err = RegGetValueW(hProgidKey, NULL, NULL, RRF_RT_REG_SZ, NULL, progid, &cb);
            RegCloseKey(hProgidKey);
        }
    }
    if (err)
    {
        err = RegLoadMUIStringW(hKey, L"FriendlyTypeName", Buffer, cch, NULL, 0, NULL);
        if (err && !errprogid)
        {
            HRESULT hr = StringCchCopyW(Buffer, cch, progid);
            RegCloseKey(hKey);
            return hr == S_OK ? hr : hr == STRSAFE_E_INSUFFICIENT_BUFFER ? S_FALSE : hr;
        }
    }
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(err);
}

static HRESULT SHELL32_AssocGetFolderFriendlyName(LPWSTR Buffer, SIZE_T cch)
{
    HRESULT hr;
    if (SUCCEEDED(hr = SHELL32_AssocGetFriendlyDocNameFromDotExt(L"Folder", Buffer, cch)))
        return hr;
    UINT len = LoadStringW(shell32_hInstance, IDS_DIRECTORY, Buffer, cch);
    return !len ? E_FAIL : len + 1 < cch ? S_OK : S_FALSE;
}

static HRESULT SHELL32_AssocGetDirectoryFriendlyName(LPWSTR Buffer, SIZE_T cch)
{
    HRESULT hr;
    if (SUCCEEDED(hr = SHELL32_AssocGetFriendlyDocNameFromDotExt(L"Directory", Buffer, cch)))
        return hr;
    return SHELL32_AssocGetFolderFriendlyName(Buffer, cch);
}

static HRESULT SHELL32_AssocGetFriendlyDocNameFromFileName(LPCWSTR Name, LPWSTR Buffer, SIZE_T cch)
{
    HRESULT hr;
    LPCWSTR ext = PathFindExtensionW(Name);
    if (*ext && ext > Name)
    {
        if (SUCCEEDED(hr = SHELL32_AssocGetFriendlyDocNameFromDotExt(ext, Buffer, cch)))
            return hr;
        WCHAR buf[MAX_PATH];
        if (LCMapStringW(LOCALE_USER_DEFAULT, LCMAP_UPPERCASE, ++ext, -1, buf, _countof(buf)))
            ext = buf;
        wsprintfW(Buffer, L"%s File!!", ext); // FIXME: Safe string API and LoadString (check FTE PR to find the one valid IDS)
    }
    else
    {
        wsprintfW(Buffer, L"File!!", ext); // FIXME: What does Windows do in this case? For "" and ".foo"?
    }
    return S_FALSE;
}

EXTERN_C HRESULT SHELL32_AssocGetTypeNameFromFileName(_In_ LPCWSTR Name, _In_ UINT FileAtt, _Out_ LPWSTR Buffer, _In_ SIZE_T cch)
{
    if (FileAtt & FILE_ATTRIBUTE_DIRECTORY)
        return SHELL32_AssocGetDirectoryFriendlyName(Buffer, cch);
    return SHELL32_AssocGetFriendlyDocNameFromFileName(Name, Buffer, cch);
}

/*
todo:
swShell32Name);
                psfi->iIcon = -IDI_SHELL_FOLDER;
*/