/*
 * Helper functions for debugging
 *
 * Copyright 1998, 2002 Juergen Schmied
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(pidl);

static inline BYTE _dbg_ILGetType(LPCITEMIDLIST pidl)
{
    return pidl && pidl->mkid.cb >= 3 ? pidl->mkid.abID[0] : 0;
}

static inline BYTE _dbg_ILGetFSType(LPCITEMIDLIST pidl)
{
    const BYTE type = _dbg_ILGetType(pidl);
    return (type & PT_FOLDERTYPEMASK) == PT_FS ? type : 0;
}

static
LPITEMIDLIST _dbg_ILGetNext(LPCITEMIDLIST pidl)
{
    WORD len;

    if(pidl)
    {
      len =  pidl->mkid.cb;
      if (len)
      {
        return (LPITEMIDLIST) (((LPBYTE)pidl)+len);
      }
    }
    return NULL;
}

static
BOOL _dbg_ILIsDesktop(LPCITEMIDLIST pidl)
{
    return ( !pidl || (pidl && pidl->mkid.cb == 0x00) );
}

static
LPPIDLDATA _dbg_ILGetDataPointer(LPCITEMIDLIST pidl)
{
    if(pidl && pidl->mkid.cb != 0x00)
      return (LPPIDLDATA) &(pidl->mkid.abID);
    return NULL;
}

static
LPSTR _dbg_ILGetTextPointer(LPCITEMIDLIST pidl)
{
    LPPIDLDATA pdata =_dbg_ILGetDataPointer(pidl);

    if (pdata)
    {
      switch (pdata->type)
      {
        case PT_GUID:
        case PT_SHELLEXT:
        case PT_YAGUID:
          return NULL;

        case PT_DRIVE:
        case PT_DRIVE1:
        case PT_DRIVE2:
        case PT_DRIVE3:
          return (LPSTR)&(pdata->u.drive.szDriveName);

        case PT_FOLDER:
        case PT_FOLDER1:
        case PT_VALUE:
        case PT_IESPECIAL1:
        case PT_IESPECIAL2:
          return (LPSTR)&(pdata->u.file.szNames);

        case PT_WORKGRP:
        case PT_COMP:
        case PT_NETWORK:
        case PT_NETPROVIDER:
        case PT_SHARE:
          return (LPSTR)&(pdata->u.network.szNames);
      }
    }
    return NULL;
}

static
LPWSTR _dbg_ILGetTextPointerW(LPCITEMIDLIST pidl)
{
    LPPIDLDATA pdata =_dbg_ILGetDataPointer(pidl);

    if (pdata)
    {
      if (_dbg_ILGetFSType(pidl) & PT_FS_UNICODE_FLAG)
        return (LPWSTR)&(pdata->u.file.szNames);

      switch (pdata->type)
      {
        case PT_GUID:
        case PT_SHELLEXT:
        case PT_YAGUID:
          return NULL;

        case PT_DRIVE:
        case PT_DRIVE1:
        case PT_DRIVE2:
        case PT_DRIVE3:
          /* return (LPSTR)&(pdata->u.drive.szDriveName);*/
          return NULL;

        case PT_FOLDER:
        case PT_FOLDER1:
        case PT_VALUE:
        case PT_IESPECIAL1:
        case PT_IESPECIAL2:
          /* return (LPSTR)&(pdata->u.file.szNames); */
          return NULL;

        case PT_WORKGRP:
        case PT_COMP:
        case PT_NETWORK:
        case PT_NETPROVIDER:
        case PT_SHARE:
          /* return (LPSTR)&(pdata->u.network.szNames); */
          return NULL;
      }
    }
    return NULL;
}


static
LPSTR _dbg_ILGetSTextPointer(LPCITEMIDLIST pidl)
{
    LPPIDLDATA pdata =_dbg_ILGetDataPointer(pidl);

    if (pdata)
    {
      switch (pdata->type)
      {
        case PT_FOLDER:
        case PT_VALUE:
        case PT_IESPECIAL1:
        case PT_IESPECIAL2:
          return (LPSTR)(pdata->u.file.szNames + strlen (pdata->u.file.szNames) + 1);

        case PT_WORKGRP:
          return (LPSTR)(pdata->u.network.szNames + strlen (pdata->u.network.szNames) + 1);
      }
    }
    return NULL;
}

static
LPWSTR _dbg_ILGetSTextPointerW(LPCITEMIDLIST pidl)
{
    LPPIDLDATA pdata =_dbg_ILGetDataPointer(pidl);

    if (pdata)
    {
      switch (pdata->type)
      {
        case PT_FOLDER:
        case PT_VALUE:
        case PT_IESPECIAL1:
        case PT_IESPECIAL2:
          /*return (LPSTR)(pdata->u.file.szNames + strlen (pdata->u.file.szNames) + 1); */
          return NULL;

        case PT_WORKGRP:
          /* return (LPSTR)(pdata->u.network.szNames + strlen (pdata->u.network.szNames) + 1); */
          return NULL;

        case PT_VALUEW:
          return (LPWSTR)(pdata->u.file.szNames + wcslen ((LPWSTR)pdata->u.file.szNames) + 1);
      }
    }
    return NULL;
}


static
IID* _dbg_ILGetGUIDPointer(LPCITEMIDLIST pidl)
{
    LPPIDLDATA pdata =_ILGetDataPointer(pidl);

    if (pdata)
    {
      switch (pdata->type)
      {
        case PT_SHELLEXT:
        case PT_GUID:
            case PT_YAGUID:
          return &(pdata->u.guid.guid);
      }
    }
    return NULL;
}

static
void _dbg_ILSimpleGetText (LPCITEMIDLIST pidl, LPSTR szOut, UINT uOutSize)
{
    LPSTR        szSrc;
    LPWSTR        szSrcW;
    GUID const *     riid;

    if (!pidl) return;

    if (szOut)
      *szOut = 0;

    if (_dbg_ILIsDesktop(pidl))
    {
     /* desktop */
      if (szOut) lstrcpynA(szOut, "Desktop", uOutSize);
    }
    else if (( szSrc = _dbg_ILGetTextPointer(pidl) ))
    {
      /* filesystem */
      if (szOut) lstrcpynA(szOut, szSrc, uOutSize);
    }
    else if (( szSrcW = _dbg_ILGetTextPointerW(pidl) ))
    {
      CHAR tmp[MAX_PATH];
      /* unicode filesystem */
      WideCharToMultiByte(CP_ACP,0,szSrcW, -1, tmp, MAX_PATH, NULL, NULL);
      if (szOut) lstrcpynA(szOut, tmp, uOutSize);
    }
    else if (( riid = _dbg_ILGetGUIDPointer(pidl) ))
    {
      if (szOut)
            sprintf( szOut, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                 riid->Data1, riid->Data2, riid->Data3,
                 riid->Data4[0], riid->Data4[1], riid->Data4[2], riid->Data4[3],
                 riid->Data4[4], riid->Data4[5], riid->Data4[6], riid->Data4[7] );
    }
}




static void pdump_impl (LPCITEMIDLIST pidl)
{
    LPCITEMIDLIST pidltemp = pidl;


    if (! pidltemp)
    {
      MESSAGE ("-------- pidl=NULL (Desktop)\n");
    }
    else
    {
      MESSAGE ("-------- pidl=%p\n", pidl);
      if (pidltemp->mkid.cb)
      {
        do
        {
          if (_ILIsUnicode(pidltemp))
          {
              DWORD dwAttrib = 0;
              LPPIDLDATA pData   = _dbg_ILGetDataPointer(pidltemp);
              DWORD type = pData ? pData->type : 0;
              LPWSTR szLongName   = _dbg_ILGetTextPointerW(pidltemp);
              LPWSTR szShortName  = _dbg_ILGetSTextPointerW(pidltemp);
              char szName[MAX_PATH];

              _dbg_ILSimpleGetText(pidltemp, szName, MAX_PATH);
              if (_dbg_ILGetFSType(pidltemp))
                dwAttrib = pData->u.file.uFileAttribs;

              MESSAGE ("[%p] size=%04u type=%x attr=0x%08x name=%s (%s,%s)\n",
                       pidltemp, pidltemp->mkid.cb, type, dwAttrib,
                           debugstr_a(szName), debugstr_w(szLongName), debugstr_w(szShortName));
          }
          else
          {
              DWORD dwAttrib = 0;
              LPPIDLDATA pData   = _dbg_ILGetDataPointer(pidltemp);
              DWORD type = pData ? pData->type : 0;
              LPSTR szLongName   = _dbg_ILGetTextPointer(pidltemp);
              LPSTR szShortName  = _dbg_ILGetSTextPointer(pidltemp);
              char szName[MAX_PATH];

              _dbg_ILSimpleGetText(pidltemp, szName, MAX_PATH);
              if (_dbg_ILGetFSType(pidltemp))
                dwAttrib = pData->u.file.uFileAttribs;

              MESSAGE ("[%p] size=%04u type=%x attr=0x%08x name=%s (%s,%s)\n",
                       pidltemp, pidltemp->mkid.cb, type, dwAttrib,
                           debugstr_a(szName), debugstr_a(szLongName), debugstr_a(szShortName));
          }

          pidltemp = _dbg_ILGetNext(pidltemp);

        } while (pidltemp && pidltemp->mkid.cb);
      }
      else
      {
        MESSAGE ("empty pidl (Desktop)\n");
      }
      pcheck(pidl);
    }
}

void pdump(LPCITEMIDLIST pidl)
{
    if (!TRACE_ON(pidl)) return;

    return pdump_impl(pidl);
}


void pdump_always(LPCITEMIDLIST pidl)
{
    pdump_impl(pidl);
}


static void dump_pidl_hex( LPCITEMIDLIST pidl )
{
    const unsigned char *p = (const unsigned char *)pidl;
    const int max_bytes = 0x80;
#define max_line 0x10
    char szHex[max_line*3+1], szAscii[max_line+1];
    int i, n;

    n = pidl->mkid.cb;
    if( n>max_bytes )
        n = max_bytes;
    for( i=0; i<n; i++ )
    {
        sprintf( &szHex[ (i%max_line)*3 ], "%02X ", p[i] );
        szAscii[ (i%max_line) ] = isprint( p[i] ) ? p[i] : '.';

        /* print out at the end of each line and when we're finished */
        if( i!=(n-1) && (i%max_line) != (max_line-1) )
            continue;
        szAscii[ (i%max_line)+1 ] = 0;
        ERR("%-*s   %s\n", max_line*3, szHex, szAscii );
    }
}

BOOL pcheck( LPCITEMIDLIST pidl )
{
    DWORD type;
    LPCITEMIDLIST pidltemp = pidl;

    while( pidltemp && pidltemp->mkid.cb )
    {
        LPPIDLDATA pidlData = _dbg_ILGetDataPointer(pidltemp);

        if (pidlData)
        {
            type = pidlData->type;
            switch( type )
            {
                case PT_CPLAPPLET:
                case PT_GUID:
                case PT_SHELLEXT:
                case PT_DRIVE:
                case PT_DRIVE1:
                case PT_DRIVE2:
                case PT_DRIVE3:
                case PT_FOLDER:
                case PT_VALUE:
                case PT_VALUEW:
                case PT_FOLDER1:
                case PT_WORKGRP:
                case PT_COMP:
                case PT_NETPROVIDER:
                case PT_NETWORK:
                case PT_IESPECIAL1:
                case PT_YAGUID:
                case PT_IESPECIAL2:
                case PT_SHARE:
                    break;
                default:
                    ERR("unknown IDLIST %p [%p] size=%u type=%x\n",
                        pidl, pidltemp, pidltemp->mkid.cb,type );
                    dump_pidl_hex( pidltemp );
                    return FALSE;
            }
            pidltemp = _dbg_ILGetNext(pidltemp);
        }
        else
        {
            return FALSE;
        }
    }
    return TRUE;
}

static const struct {
    REFIID riid;
    const char *name;
} InterfaceDesc[] = {
    {IID_IUnknown,            "IID_IUnknown"},
    {IID_IClassFactory,        "IID_IClassFactory"},
    {IID_IShellView,        "IID_IShellView"},
    {IID_IOleCommandTarget,    "IID_IOleCommandTarget"},
    {IID_IDropTarget,        "IID_IDropTarget"},
    {IID_IDropSource,        "IID_IDropSource"},
    {IID_IViewObject,        "IID_IViewObject"},
    {IID_IContextMenu,        "IID_IContextMenu"},
    {IID_IShellExtInit,        "IID_IShellExtInit"},
    {IID_IShellFolder,        "IID_IShellFolder"},
    {IID_IShellFolder2,        "IID_IShellFolder2"},
    {IID_IPersist,            "IID_IPersist"},
    {IID_IPersistFolder,        "IID_IPersistFolder"},
    {IID_IPersistFolder2,        "IID_IPersistFolder2"},
    {IID_IPersistFolder3,        "IID_IPersistFolder3"},
    {IID_IExtractIconA,        "IID_IExtractIconA"},
    {IID_IExtractIconW,        "IID_IExtractIconW"},
    {IID_IDataObject,        "IID_IDataObject"},
    {IID_IAutoComplete,            "IID_IAutoComplete"},
    {IID_IAutoComplete2,           "IID_IAutoComplete2"},
        {IID_IShellLinkA,              "IID_IShellLinkA"},
        {IID_IShellLinkW,              "IID_IShellLinkW"},
    };

const char * shdebugstr_guid( const struct _GUID *id )
{
    unsigned int i;
    const char* name = NULL;
    char clsidbuf[100];

    if (!id) return "(null)";

        for (i=0; i < sizeof(InterfaceDesc) / sizeof(InterfaceDesc[0]); i++) {
            if (IsEqualIID(InterfaceDesc[i].riid, *id)) name = InterfaceDesc[i].name;
        }
        if (!name) {
        if (HCR_GetClassNameA(*id, clsidbuf, 100))
            name = clsidbuf;
        }

            return wine_dbg_sprintf( "\n\t{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x} (%s)",
                 (UINT)id->Data1, id->Data2, id->Data3,
                 id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3],
                 id->Data4[4], id->Data4[5], id->Data4[6], id->Data4[7], name ? name : "unknown" );
}

#if DBG
static inline CHAR GetSafeDumpChar(BYTE Ch, BYTE DefChar = '.')
{
    return Ch >= ' ' && Ch < 127 ? Ch : DefChar;
}

static void EditAppend(HWND hEdit, PCWSTR String)
{
    SendMessageW(hEdit, EM_SETSEL, -1, -1);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)String);
}

static void HexDumpToEditControl(HWND hEdit, LPCVOID Data, SIZE_T Size)
{
    const BYTE *p = (BYTE*)Data;
    const UINT BytesPerRow = 8, cchGap = 1;
    WCHAR buf[(BytesPerRow * 3) + cchGap + BytesPerRow + 1];
    for (SIZE_T i = 0, j, cch; i < Size; i += BytesPerRow)
    {
        for (j = 0; j < BytesPerRow; ++j)
        {
            if (i + j < Size)
                wsprintfW(buf + (j * 3), L"%.2X ", p[i + j]);
            else
                wsprintfW(buf + (j * 3), L".. ");
        }
        cch = BytesPerRow * 3;
        for (j = 0; j < cchGap; ++j)
            buf[cch++] = ' ';
        for (j = 0; j < BytesPerRow && i + j < Size; ++j)
            buf[cch++] = GetSafeDumpChar(p[i + j]);
        buf[cch++] = '\n';
        buf[cch] = UNICODE_NULL;
        EditAppend(hEdit, buf);
    }
}

static BOOL SH32Dbg_IsTriggerEnabled(PCWSTR Id)
{
    WCHAR key[255];
    PathCombineW(key, L"Software\\ReactOS\\Debug", L"shell32.dll");
    return SHRegGetBoolUSValueW(key, Id, FALSE, FALSE);
}

static BOOL SH32Dbg_IsTrigger(PCWSTR Id)
{
    return GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0 &&
           SH32Dbg_IsTriggerEnabled(Id);
}

typedef struct _SH32DBGWINDOWINITDATA
{
    WNDPROC WndProc;
    LPCVOID Param;
    PCWSTR Title;
    UINT Style;
} SH32DBGWINDOWINITDATA;

static DWORD CALLBACK SH32DbgWindowThreadProc(LPVOID Param)
{
    SH32DBGWINDOWINITDATA *p = (SH32DBGWINDOWINITDATA*)Param;
    if (GetLastError() != (UINT)-1)
    {
        HWND hWnd = CreateWindowExW(0, WC_STATIC, p->Title, p->Style & ~WS_VISIBLE,
                                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                    NULL, NULL, NULL, NULL);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)p->Param);
        SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)p->WndProc);
        p->WndProc(hWnd, WM_CREATE, (LONG_PTR)p->Param, (LONG_PTR)p->Param);
        ShowWindow(hWnd, !!(p->Style & WS_VISIBLE));
        SetLastError(-1);
        return 0;
    }
    MSG msg;
    while ((int)GetMessageW(&msg, 0, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

static void SH32Dbg_Window(WNDPROC WndProc, LPCVOID Param, PCWSTR Title, UINT Style)
{
    SH32DBGWINDOWINITDATA data = { WndProc, Param, Title, Style };
    SHCreateThread(SH32DbgWindowThreadProc, &data, CTF_COINIT | CTF_PROCESS_REF | CTF_FREELIBANDEXIT,
                   SH32DbgWindowThreadProc);
}

extern char* SH32Dbg_AccessSIC(INT_PTR Op, INT_PTR Param1);

struct SystemImageListHelper
{
    struct SIC_ENTRY { PCWSTR Path; UINT iIcon, iIndex, GIL, Time; };
    SystemImageListHelper() { SH32Dbg_AccessSIC(0, 0); }
    ~SystemImageListHelper() { SH32Dbg_AccessSIC(1, 0); }

    static INT CALLBACK FindByIndexCallback(LPVOID p1, LPVOID p2, LPARAM lParam)
    {
        return (INT)lParam - (INT)((SIC_ENTRY*)p2)->iIndex;
    }
    SIC_ENTRY* FindByIndex(int iIndex)
    {
        HDPA hDPA = (HDPA)SH32Dbg_AccessSIC(2, 0);
        return (SIC_ENTRY*)DPA_GetPtr(hDPA, DPA_Search(hDPA, &iIndex, 0, FindByIndexCallback, iIndex, 0));
    }
};

static LRESULT CALLBACK ViewSILWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HWND hLV = GetDlgItem(hWnd, 1);
    NMLVKEYDOWN *pLVKD = (NMLVKEYDOWN*)lParam;
    switch (uMsg)
    {
        case WM_CREATE:
        {
            hLV = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL, WS_CHILD | WS_VISIBLE |
                                  LVS_SHAREIMAGELISTS | LVS_NOSORTHEADER | LVS_SINGLESEL |
                                  LVS_REPORT | LVS_AUTOARRANGE, 0, 0, 0, 0, hWnd, (HMENU)1, NULL, NULL);
            PCWSTR cols[] = { L"#", L"Path", L"Icon", L"Flags" };
            LVCOLUMN lvc;
            lvc.mask = LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
            for (UINT i = 0; i < _countof(cols); ++i)
            {
                lvc.pszText = const_cast<PWSTR>(cols[i]);
                lvc.cx = i == 1 ? 200 : 75;
                ListView_InsertColumn(hLV, lvc.iSubItem = i, &lvc);
            }
            PostMessageW(hWnd, WM_APP, '2', 0);
            break;
        }
        case WM_SIZE:
            SetWindowPos(hLV, hLV, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER | SWP_NOACTIVATE);
            break;
        case WM_NOTIFY:
            if (pLVKD->hdr.code == LVN_KEYDOWN)
                return SendMessageW(hWnd, WM_APP, pLVKD->wVKey, 0) | TRUE;
            break;
        case WM_SETFOCUS:
            return (LRESULT)SetFocus(hLV);
        case WM_APP:
            if (wParam >= '1' && wParam <= '9')
            {
                if (HIMAGELIST hIL = SIL_GetImageList(UINT(wParam - '1')))
                {
                    ListView_SetImageList(hLV, hIL, LVSIL_NORMAL);
                    ListView_SetImageList(hLV, hIL, LVSIL_SMALL);
                    wParam = VK_F5;
                }
            }
            if (wParam >= VK_F1 && wParam <= VK_F4)
                ListView_SetView(hLV, UINT(wParam - VK_F1 + 1));
            if (wParam == VK_F5)
            {
                WCHAR buf[42];
                ListView_DeleteAllItems(hLV);
                SendMessageW(hLV, WM_SETREDRAW, FALSE, 0);
                SystemImageListHelper List;
                HIMAGELIST hIL = ListView_GetImageList(hLV, LVSIL_NORMAL);
                UINT c = ImageList_GetImageCount(hIL);
                int w, h;
                ImageList_GetIconSize(hIL, &w, &h);
                wsprintfW(buf, L"%s (%d, %dx%d)", L"SIL", c, w, h);
                SetWindowTextW(hWnd, buf);
                for (UINT i = 0; i < c; ++i)
                {
                    SystemImageListHelper::SIC_ENTRY *p = List.FindByIndex(i);
                    wsprintfW(buf, L"%d", i);
                    LVITEMW lvi;
                    lvi.mask = LVIF_TEXT | LVIF_IMAGE;
                    lvi.iItem = lvi.iImage = i;
                    lvi.iSubItem = 0;
                    lvi.pszText = buf;
                    ListView_InsertItem(hLV, &lvi);
                    ListView_SetItemText(hLV, i, 1, const_cast<PWSTR>(p->Path));
                    wsprintfW(buf, L"%d", p->iIcon);
                    ListView_SetItemText(hLV, i, 2, buf);
                    wsprintfW(buf, L"%#.4x", p->GIL);
                    ListView_SetItemText(hLV, i, 3, buf);
                }
                SendMessageW(hLV, WM_SETREDRAW, TRUE, 0);
                InvalidateRect(hLV, NULL, TRUE);
            }
            break;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

EXTERN_C void SH32DbgTrigger_ViewSIL()
{
    if (SH32Dbg_IsTrigger(L"SIL"))
        SH32Dbg_Window(ViewSILWndProc, NULL, L"SIL", WS_OVERLAPPEDWINDOW | WS_VISIBLE);
}

static LRESULT CALLBACK DvDumpPIDLWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HWND hEdit = GetDlgItem(hWnd, 1);
    switch (uMsg)
    {
        case WM_CREATE:
        {
            hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDIT, NULL, WS_CHILD | WS_VISIBLE |
                                    WS_HSCROLL | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL,
                                    0, 0, 0, 0, hWnd, (HMENU)1, NULL, NULL);
            NONCLIENTMETRICSW ncm;
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize = sizeof(ncm), &ncm, 0);
            ncm.lfMessageFont.lfPitchAndFamily = FIXED_PITCH | FF_DONTCARE;
            SendMessageW(hEdit, WM_SETFONT, (WPARAM)CreateFontIndirectW(&ncm.lfMessageFont), 0);
            LPCITEMIDLIST pidl = (LPCITEMIDLIST)wParam;
            UINT parent = ~0UL;
            for (UINT depth = 0; pidl->mkid.cb; ++depth, pidl = ILGetNext(pidl))
            {
                if (depth)
                    EditAppend(hEdit, L"\n");
                UINT size = pidl->mkid.cb, type = _ILGetType(pidl);
                WCHAR buf[MAX_PATH];
                wsprintfW(buf, L"#%u %ub", depth + 1, size);
                EditAppend(hEdit, buf);
                *buf = UNICODE_NULL;
                if (depth == 0 && type == PT_DESKTOP_REGITEM) guid:
                {
                    if (size >= sizeof(WORD) + 2 + sizeof(GUID))
                    {
                        GUID *pGuid = (GUID*)((char*)pidl + size - sizeof(GUID));
                        buf[0] = ' ';
                        StringFromGUID2(*pGuid, &buf[1], _countof(buf) - 1);
                    }
                }
                else if (depth == 1 && type == PT_COMPUTER_REGITEM && parent == PT_DESKTOP_REGITEM)
                {
                    goto guid;
                }
                EditAppend(hEdit, buf);
                EditAppend(hEdit, L"\n");
                HexDumpToEditControl(hEdit, pidl, size);
                parent = type;
            }
            SendMessageW(hEdit, EM_SETSEL, 0, 0);
            break;
        }
        case WM_DESTROY:
            DeleteObject((HFONT)SendMessage(hEdit, WM_GETFONT, 0, 0));
            break;
        case WM_SIZE:
            SetWindowPos(hEdit, hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER | SWP_NOACTIVATE);
            break;
        case WM_SETFOCUS:
            return (LRESULT)SetFocus(hEdit);
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

EXTERN_C void SH32DbgTrigger_DvDumpPIDL(IShellFolder* pSF, LPCITEMIDLIST pidl)
{
    if (GetAsyncKeyState(VK_CAPITAL) >= 0 || !SH32Dbg_IsTrigger(L"DV"))
        return;

    PIDLIST_ABSOLUTE pidlFree = NULL;
    if (pSF)
    {
        PIDLIST_ABSOLUTE pidlFolder;
        HRESULT hr = SHGetIDListFromObject(pSF, &pidlFolder);
        if (SUCCEEDED(hr))
        {
            pidl = pidlFree = ILCombine(pidlFolder, pidl);
            ILFree(pidlFolder);
        }
    }

    if (!_ILIsEmpty(pidl))
        SH32Dbg_Window(DvDumpPIDLWndProc, pidl, L"PIDL", WS_OVERLAPPEDWINDOW | WS_VISIBLE);

    ILFree(pidlFree);
}
#endif // DBG
