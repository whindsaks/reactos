/*
 * ReactOS Management Console
 * Copyright (C) 2006 - 2007 Thomas Weidenmueller
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "precomp.h"
#include <ole2.h>
#include <unknwn.h>
//#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlwapi_undoc.h>

HINSTANCE hAppInstance;
HANDLE hAppHeap;
HWND hwndMainConsole;
HWND hwndMDIClient;

static HRESULT
LoadLinkFromFile(IShellLink *pSL, PCWSTR LnkPath)
{
    HRESULT hr;
    IPersistFile *pPF;
    if (SUCCEEDED(hr = IUnknown_QueryInterface(pSL, &IID_IPersistFile, (void**)&pPF)))
    {
        hr = IPersistFile_Load(pPF, LnkPath, STGM_READ);
        IUnknown_Release(pPF);
    }
    return hr;
}

static BOOL
HandleRosMscFile(PCWSTR pszParams)
{
    HRESULT hr = E_FAIL;
    IShellLinkW *pSL;
    int argc;
    PWSTR *argv = CommandLineToArgvW(pszParams, &argc);
    if (!argv)
        return FALSE;

    if (argc)
        hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void**)&pSL);
    if (SUCCEEDED(hr))
    {
        if (SUCCEEDED(hr = LoadLinkFromFile(pSL, argv[0])))
        {
            IContextMenu *pCM;
            if (SUCCEEDED(IUnknown_QueryInterface(pSL, &IID_IContextMenu, (void**)&pCM)))
            {
                IContextMenu_Invoke(pCM, NULL, NULL, CMF_NORMAL);
                IUnknown_Release(pCM);
            }
        }
        IUnknown_Release(pSL);
    }
    LocalFree(argv);
    return SUCCEEDED(hr);
}


int WINAPI
_tWinMain(HINSTANCE hInstance,
          HINSTANCE hPrevInstance,
          LPTSTR lpCmdLine,
          int nCmdShow)
{
    int retval = 0;
    MSG Msg;
    HRESULT hrInit = OleInitialize(NULL);

    hAppInstance = hInstance; // GetModuleHandle(NULL);
    hAppHeap = GetProcessHeap();

    InitCommonControls();

    if (HandleRosMscFile(lpCmdLine))
        goto exit;

    if (!RegisterMMCWndClasses())
    {
        /* FIXME - Display error */
        retval = 1;
        goto exit;
    }

    hwndMainConsole = CreateConsoleWindow(NULL /*argc > 1 ? argv[1] : NULL*/, nCmdShow);
    if (hwndMainConsole != NULL)
    {
        while (GetMessage(&Msg, NULL, 0, 0))
        {
            if (!TranslateMDISysAccel(hwndMDIClient, &Msg))
            {
                TranslateMessage(&Msg);
                DispatchMessage(&Msg);
            }
        }
    }

    UnregisterMMCWndClasses();
exit:
    if (SUCCEEDED(hrInit))
        OleUninitialize();
    return retval;
}
