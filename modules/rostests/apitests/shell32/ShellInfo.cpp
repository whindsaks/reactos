/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for SHGetFileInfo
 * COPYRIGHT:   Copyright 2024 Whindmar Saksit <whindsaks@proton.me>
 */

#include "shelltest.h"
#include <shellutils.h>
#include <undocshell.h>
#include <shlwapi.h> // Path*
#include <stdio.h> // _wfopen

#define UNIQUEEXT L"ABC123XYZ"
#define my_ok_all_flags(val, flags) ok_eq_hex((val) & (flags), (flags))
#define init_(x, msg) ( (x) ? TRUE : (skip("Init %s\n", msg), FALSE) )
#define init(x) init_((x), "")

static const BYTE g_FileIconData1x1[] =
{
  0, 0, 1, 0, 1, 0, 1, 1, 2, 0, 1, 0, 1, 0, 56, 0, 0, 0, 22, 0, 0, 0, 40, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1, 0, 1, 0, 0, 0,
  0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static BOOL MakeFile(PCWSTR Path, LPCVOID Data, SIZE_T Size)
{
    FILE *pFile = _wfopen(Path, L"wb");
    if (!pFile)
        return FALSE;
    BOOL Success = fwrite(Data, Size, 1, pFile) == 1;
    fclose(pFile);
    return Success;
}

static DWORD_PTR SHGFI(PCWSTR Path, SHFILEINFOW &Info, UINT Flags, UINT Attributes = 0)
{
    return SHGetFileInfoW(Path, Attributes, &Info, sizeof(Info), Flags);
}

static DWORD_PTR SHGFI(LPCITEMIDLIST Pidl, SHFILEINFOW &Info, UINT Flags, UINT Attributes = 0)
{
    return SHGFI((PCWSTR)Pidl, Info, Flags | SHGFI_PIDL, Attributes);
}

START_TEST(SHGetFileInfo)
{
    FileIconInit(FALSE); // Just a basic system image list please
    CCoInit ComInit;
    LPITEMIDLIST pidl;
    SHFILEINFOW info;
    UINT flags;
    WCHAR buf[MAX_PATH];

    ok_int(SHGFI((PCWSTR)NULL, info, 0), FALSE);
    ok_int(SHGFI((PCWSTR)NULL, info, SHGFI_DISPLAYNAME), FALSE);
    ok_int(SHGFI((PCWSTR)NULL, info, SHGFI_TYPENAME), FALSE);
    ok_int(SHGFI((PCWSTR)NULL, info, SHGFI_ICONLOCATION), FALSE);
    ok_int(SHGFI((PCWSTR)NULL, info, SHGFI_SYSICONINDEX), FALSE);
    ok_int(SHGFI(UNIQUEEXT, info, SHGFI_USEFILEATTRIBUTES), TRUE); // Success when asking for no info
    ok_int(SHGetFileInfoW(UNIQUEEXT, 0, NULL, 0, SHGFI_DISPLAYNAME | SHGFI_USEFILEATTRIBUTES), FALSE); // NULL pointer
    ok_int(SHGFI(UNIQUEEXT, info, SHGFI_EXETYPE | SHGFI_USEFILEATTRIBUTES), TRUE); // Invalid combination, returns TRUE!

    GetModuleFileNameW(NULL, buf, _countof(buf));
    ok_int(LOWORD(SHGFI(buf, info, SHGFI_EXETYPE)), 0x4550); // 'PE'

    flags = SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES | SHGFI_ATTRIBUTES;
    ZeroMemory(&info, sizeof(info));
    info.dwAttributes = ~SFGAO_VALIDATE;
    ok_int(SHGFI(UNIQUEEXT, info, flags | SHGFI_ATTR_SPECIFIED), TRUE);
    ok_ptr(StrStrIW(info.szTypeName, UNIQUEEXT), NULL); // A file without extension (not "EXT File")
    my_ok_all_flags(info.dwAttributes, SFGAO_FILESYSTEM | SFGAO_STREAM);

    ZeroMemory(&info, sizeof(info));
    info.dwAttributes = ~SFGAO_VALIDATE;
    ok_int(SHGFI(UNIQUEEXT, info, flags | SHGFI_ATTR_SPECIFIED, FILE_ATTRIBUTE_DIRECTORY), TRUE);
    ok_ptr(StrStrIW(info.szTypeName, UNIQUEEXT), NULL); // A directory (not "EXT File")
    my_ok_all_flags(info.dwAttributes, SFGAO_FILESYSTEM | SFGAO_FOLDER);

    ZeroMemory(&info, sizeof(info));
    info.dwAttributes = ~SFGAO_VALIDATE;
    ok_int(SHGFI(L"." UNIQUEEXT, info, flags | SHGFI_ATTR_SPECIFIED), TRUE);
    ok_bool_true(StrStrIW(info.szTypeName, UNIQUEEXT) != NULL, ".ext is treated as extension");
    my_ok_all_flags(info.dwAttributes, SFGAO_FILESYSTEM | SFGAO_STREAM);

    info.dwAttributes = ~SFGAO_VALIDATE;
    ok_int(SHGFI(L"c:", info, flags | SHGFI_ATTR_SPECIFIED), TRUE);
    my_ok_all_flags(info.dwAttributes, SFGAO_FILESYSTEM | SFGAO_FILESYSANCESTOR);

    info.dwAttributes = ~SFGAO_VALIDATE;
    ok_int(SHGFI(L"c:\\", info, flags | SHGFI_ATTR_SPECIFIED), TRUE);
    my_ok_all_flags(info.dwAttributes, SFGAO_FILESYSTEM | SFGAO_FILESYSANCESTOR);

    pidl = SHSimpleIDListFromPath(L"x:\\dir\\file." UNIQUEEXT);
    info.iIcon = -1;
    HIMAGELIST hSIL = (HIMAGELIST)SHGFI(pidl, info, SHGFI_SYSICONINDEX);
    ok_int((UINT_PTR)hSIL > TRUE, TRUE);
    ok_int(info.iIcon != -1, TRUE);
    const int silnoassocfileidx = info.iIcon;
    ILRemoveLastID(pidl);
    info.iIcon = -1;
    ok_int(SHGFI(pidl, info, SHGFI_SYSICONINDEX) > TRUE, TRUE);
    ok_int(info.iIcon > 0, TRUE);
    ok_int(info.iIcon != silnoassocfileidx, TRUE);
    ILFree(pidl);

    ok_int(SHGFI(L"__file__", info, SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES) > TRUE, TRUE);
    ok_int(info.iIcon == silnoassocfileidx, TRUE); // 7-Zip 24.09

    pidl = SHSimpleIDListFromPath(L"x:\\dir\\file.txt");
    ok_int(SHGFI(pidl, info, SHGFI_SYSICONINDEX) > TRUE, TRUE);
    ok_int(info.iIcon > 0, TRUE);
    ILFree(pidl);
    const int siltxtidx = info.iIcon;
    ok_int(SHGFI(L".txt", info, SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES) > TRUE, TRUE);
    ok_int(info.iIcon == siltxtidx, TRUE); // 7-Zip 24.09

    ok_int(SHGFI(L"x:\\.txt", info, SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES) > TRUE, TRUE);
    ok_int(info.iIcon == siltxtidx, TRUE);
    ok_int(SHGFI(L"x:\\x.txt", info, SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES) > TRUE, TRUE);
    ok_int(info.iIcon == siltxtidx, TRUE);
    ok_int(SHGFI(L"x:\\*.txt", info, SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES) > TRUE, TRUE);
    ok_int(info.iIcon == siltxtidx, TRUE);
    ok_int(SHGFI(L"*.txt", info, SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES) > TRUE, TRUE);
    ok_int(info.iIcon == siltxtidx, TRUE);

    flags = SHGFI_ICON | SHGFI_LARGEICON | SHGFI_SHELLICONSIZE;
    info.hIcon = NULL;
    SHGFI(L".txt", info, flags | SHGFI_USEFILEATTRIBUTES); // 7-Zip 24.09
    ok(info.hIcon != NULL, "Extract icon of extension\n");
    DestroyIcon(info.hIcon);

    // Even without SHGFI_SYSICONINDEX, asking for SHGFI_ICON will add it to the SIL.
    #define UNIQUEICOEXT L"ICO" UNIQUEEXT
    GetTempPathW(_countof(buf), buf);
    GetTempFileNameW(buf, L"TEST", 0, buf);
    BOOL baseinit = hSIL && ImageList_GetImageCount(hSIL) < 50; // Test only works when the SIL is not full
    if (init(baseinit && MakeFile(buf, g_FileIconData1x1, sizeof(g_FileIconData1x1))))
    {
        HKEY hKey = HKEY_CURRENT_USER;
        PCWSTR icoextdeficokeypath = L"Software\\Classes\\." UNIQUEICOEXT L"\\DefaultIcon";
        if (init_(!SHSetValueW(hKey, icoextdeficokeypath, NULL, REG_SZ, buf, (lstrlenW(buf) + 1) * sizeof(*buf)), "Reg"))
        {
            UINT count = ImageList_GetImageCount(hSIL);
            info.hIcon = NULL;
            ok_int(SHGFI(L"." UNIQUEICOEXT, info, SHGFI_ICON | SHGFI_USEFILEATTRIBUTES) != FALSE, TRUE);
            ok_int(ImageList_GetImageCount(hSIL), count + 1);
            DestroyIcon(info.hIcon);
            info.hIcon = NULL;
            ok_int(SHGFI(L"." UNIQUEICOEXT, info, SHGFI_ICON | SHGFI_USEFILEATTRIBUTES) != FALSE, TRUE);
            ok_int(ImageList_GetImageCount(hSIL), count + 1); // Should already be in the list now
            DestroyIcon(info.hIcon);
            SHDeleteKeyW(hKey, L"Software\\Classes\\." UNIQUEICOEXT);
        }
        DeleteFileW(buf);
    }
}
