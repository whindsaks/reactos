/*
 * PROJECT:     ReactOS CabView Shell Extension
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * COPYRIGHT:   Whindmar Saksit (whindsaks@proton.me)
 */

#pragma once

template<class H> static DWORD ErrorBox(H hwnd, int code)
{
    DWORD retval = code, flags = FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM;
    if (code == ERROR_SUCCESS)
        code = ERROR_INTERNAL_ERROR;
    for (;;)
    {
        WCHAR buf[400];
        DWORD cch = FormatMessageW(flags, NULL, code, 0, buf, _countof(buf), NULL);
        if (!cch)
        {
            if (HIWORD(code) == HIWORD(HRESULT_FROM_WIN32(1)))
            {
                code = HRESULT_CODE(code); // Extract ERROR_ from HRESULT_FROM_WIN32
                continue;
            }
            if (code != ERROR_INTERNAL_ERROR)
            {
                code = ERROR_INTERNAL_ERROR;
                continue;
            }
            buf[0] = '?';
            buf[1] = UNICODE_NULL; // I give up
        }
        MessageBoxW(hwnd, buf, 0, MB_ICONSTOP);
        return retval;
    }
}

static HRESULT InitVariantFromBuffer(const void *buffer, UINT cb, VARIANT *pv)
{
    SAFEARRAY *pa = SafeArrayCreateVector(VT_UI1, 0, cb);
    if (pa)
    {
        CopyMemory(pa->pvData, buffer, cb);
        V_VT(pv) = VT_UI1 | VT_ARRAY;
        V_UNION(pv, parray) = pa;
        return S_OK;
    }
    V_VT(pv) = VT_EMPTY;
    return E_OUTOFMEMORY;
}

static HRESULT StrTo(LPCWSTR str, UINT len, STRRET &sr)
{
    LPWSTR data = (LPWSTR)SHAlloc(++len * sizeof(WCHAR));
    if (!data)
        return E_OUTOFMEMORY;
    lstrcpynW(data, str, len);
    sr.uType = STRRET_WSTR;
    sr.pOleStr = data;
    return S_OK;
}

static HRESULT StrTo(LPCWSTR str, STRRET &sr)
{
    return StrTo(str, lstrlenW(str), sr);
}

static HRESULT StrTo(LPCWSTR str, UINT len, VARIANT &v)
{
    BSTR data = SysAllocStringLen(str, len);
    if (!data)
        return E_OUTOFMEMORY;
    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = data;
    return S_OK;
}

static HRESULT StrTo(LPCWSTR str, VARIANT &v)
{
    return StrTo(str, lstrlenW(str), v);
}

static HRESULT StrRetToVariantBSTR(STRRET *psr, VARIANT &v)
{
    HRESULT hr = StrRetToBSTR(psr, NULL, &V_BSTR(&v));
    if (SUCCEEDED(hr))
        V_VT(&v) = VT_BSTR;
    return hr;
}

static HRESULT InsertMenuItem(QCMINFO &qcmi, UINT &Pos, UINT &TrackId, UINT Id, UINT ResId)
{
    UINT flags = MF_BYPOSITION;
    WCHAR string[MAX_PATH];
    string[0] = UNICODE_NULL;
    if ((Id += qcmi.idCmdFirst) > qcmi.idCmdLast)
        return E_FAIL;
    else if (ResId == (UINT)-1)
        flags |= MF_SEPARATOR;
    else if (!LoadStringW(g_hInst, ResId, string, _countof(string)))
        return E_FAIL;
    if (!InsertMenuW(qcmi.hmenu, Pos, flags, Id, string))
        return E_FAIL;
    Pos++;
    TrackId = max(TrackId, Id);
    return S_OK;
}

static SFGAOF MapFSToSFAttributes(UINT att)
{
    return ((att & FILE_ATTRIBUTE_READONLY) ? SFGAO_READONLY : 0) |
           ((att & FILE_ATTRIBUTE_HIDDEN) ? SFGAO_HIDDEN : 0) |
           ((att & FILE_ATTRIBUTE_SYSTEM) ? SFGAO_SYSTEM : 0);
}

static bool IncludeInEnumIDList(SHCONTF contf, SFGAOF att)
{
    SHCONTF both = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS;
    SFGAOF superbits = SFGAO_HIDDEN | SFGAO_READONLY | SFGAO_SYSTEM;
    bool isfile = (att & (SFGAO_STREAM | SFGAO_FOLDER)) != SFGAO_FOLDER;
    if ((contf & both) != both && !(contf & SHCONTF_STORAGE))
    {
        if (isfile && (contf & SHCONTF_FOLDERS))
            return false;
        if ((att & SFGAO_FOLDER) && (contf & SHCONTF_NONFOLDERS))
            return false;
    }
    if ((att & SFGAO_HIDDEN) && !(contf & (SHCONTF_INCLUDEHIDDEN | SHCONTF_STORAGE)))
        return false;
    if ((att & superbits) > SFGAO_HIDDEN && !(contf & (SHCONTF_INCLUDESUPERHIDDEN | SHCONTF_STORAGE)))
        return false;
    return true;
}
