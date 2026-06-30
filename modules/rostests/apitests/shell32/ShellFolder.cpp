/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Tests for IShellFolder
 * COPYRIGHT:   Copyright 2026 Whindmar Saksit <whindsaks@proton.me>
 */

#include "shelltest.h"
#include <shellutils.h>
#include <shlguid_undoc.h>

static IShellFolder2* CreateFolder2Instance(WORD csidl)
{
    PIDLIST_ABSOLUTE pidl;
    HRESULT hr = SHGetSpecialFolderLocation(NULL, csidl, &pidl);
    if (FAILED(hr))
        return NULL;
    IShellFolder2 *pFolder;
    hr = SHELL_BindToObject(NULL, pidl, NULL, IID_PPV_ARG(IShellFolder2, &pFolder));
    ILFree(pidl);
    return SUCCEEDED(hr) ? pFolder : NULL;
}

static void TestIShellIcon()
{
    const bool nt5 = LOBYTE(GetVersion()) < 6;
    static const struct {
        BYTE csidl;
        bool Missing;
    } folders[] = 
    {
        { CSIDL_DESKTOP },
        { CSIDL_DRIVES, nt5 },
        { CSIDL_CONTROLS, nt5 },
        { CSIDL_BITBUCKET, true },
        { CSIDL_SYSTEM },
        { CSIDL_PRINTERS, true },
        // TODO { CSIDL_NETWORK },
        // TODO { CSIDL_FONTS },
        // TODO { CSIDL_COOKIES },
        { CSIDL_CONNECTIONS, true },
    };
    for (SIZE_T i = 0; i < _countof(folders); ++i)
    {
        CComPtr<IShellFolder2> pFolder(CreateFolder2Instance(folders[i].csidl));
        if (!pFolder)
        {
            skip("Can't create special folder %u\n", folders[i].csidl);
            continue;
        }

        CComPtr<IShellIcon> pSI;
        HRESULT hr = pFolder->QueryInterface(IID_PPV_ARG(IShellIcon, &pSI));
        if (folders[i].Missing)
            ok(FAILED(hr), "IShellIcon implemented by folder %u\n", folders[i].csidl);
        else
            ok(SUCCEEDED(hr), "IShellIcon not implemented by folder %u\n", folders[i].csidl);
    }
}

START_TEST(ShellFolder)
{
    CCoInit ComInit;

    TestIShellIcon();
}
