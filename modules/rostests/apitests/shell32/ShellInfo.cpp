#include "shelltest.h"
#include <shellutils.h>

#define UNIQUEEXT L"ABC123XYZ"
#define my_ok_all_flags(val, flags) ok_eq_hex((val) & (flags), (flags))

static DWORD_PTR SHGFI(LPCWSTR Path, SHFILEINFOW &Info, UINT Flags, UINT Attributes = 0)
{
    return SHGetFileInfoW(Path, Attributes, &Info, sizeof(Info), Flags);
}

/*static DWORD_PTR SHGFI(LPCITEMIDLIST Pidl, SHFILEINFOW &Info, UINT Flags, UINT Attributes = 0)
{
    return SHGFI((LPCWSTR)Pidl, Info, Flags | SHGFI_PIDL, Attributes);
}*/

START_TEST(SHGetFileInfo)
{
    CCoInit ComInit;
    SHFILEINFOW info;
    UINT flags;
    WCHAR buf[MAX_PATH];

    ok_int(SHGFI((LPCWSTR)NULL, info, 0), FALSE);
    ok_int(SHGFI((LPCWSTR)NULL, info, SHGFI_DISPLAYNAME), FALSE);
    ok_int(SHGFI((LPCWSTR)NULL, info, SHGFI_TYPENAME), FALSE);
    ok_int(SHGFI((LPCWSTR)NULL, info, SHGFI_ICONLOCATION), FALSE);

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
    ok_int(SHGFI(L"c:", info, flags | SHGFI_ATTR_SPECIFIED), TRUE); // ROS fails this, a parsing bug in CDrivesFolder?
    my_ok_all_flags(info.dwAttributes, SFGAO_FILESYSTEM | SFGAO_FILESYSANCESTOR);

    info.dwAttributes = ~SFGAO_VALIDATE;
    ok_int(SHGFI(L"c:\\", info, flags | SHGFI_ATTR_SPECIFIED), TRUE);
    my_ok_all_flags(info.dwAttributes, SFGAO_FILESYSTEM | SFGAO_FILESYSANCESTOR);
}
