/*
 * PROJECT:     ole32
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     PropVariant functions
 * COPYRIGHT:   Copyright 2026 Whindmar Saksit <whindsaks@proton.me>
 */

#ifdef __REACTOS__

#define COBJMACROS
#include "windows.h"
#include "objbase.h"
#include "oleauto.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ole);

#define SetPV(pv, vtval, member, val) ( (pv)->vt = (vtval), (pv)->member = (val), S_OK )

BSTR WINAPI PropSysAllocString(PCWSTR);
void WINAPI PropSysFreeString(BSTR);
#define VChangeTypeEx VariantChangeTypeEx // Delay-loaded

typedef HRESULT (WINAPI*PSPROPVARIANTCHANGETYPEPROC)(PROPVARIANT*, PROPVARIANT*, WORD, VARTYPE);
HRESULT WINAPI Ole32PropVariantChangeType(_Out_ PROPVARIANT*,_In_ PROPVARIANT*,_In_ LCID,_In_ WORD,_In_ VARTYPE);

static HMODULE LoadSysLib(PCSTR Dll)
{
    WCHAR path[MAX_PATH + 42];
    UINT cch = GetSystemDirectoryW(path, MAX_PATH);
    wsprintfW(path + cch, L"\\%hs.dll", Dll);
    return LoadLibraryW(path);
}

static FARPROC GetSysLibProc(PCSTR Dll, PCSTR Func)
{
    HMODULE hMod = LoadSysLib(Dll);
    return hMod ? GetProcAddress(hMod, Func) : NULL;
}

static HRESULT DupStrToWide(PCWSTR psz, PWSTR *ppsz)
{
    SIZE_T cch = lstrlenW(psz), cb = ++cch * sizeof(WCHAR);
    PWSTR buf = (PWSTR)CoTaskMemAlloc(cb);
    if (buf)
    {
        CopyMemory(buf, psz, cb);
        *ppsz = buf;
        return S_OK;
    }
    return E_OUTOFMEMORY;
}

static HRESULT DupStrToAnsi(PCWSTR psz, PSTR *ppsz)
{
    UINT cb = WideCharToMultiByte(CP_ACP, 0, psz, -1, NULL, 0, NULL, NULL);
    PSTR buf = cb ? (PSTR)CoTaskMemAlloc(cb) : NULL;
    if (buf)
    {
        WideCharToMultiByte(CP_ACP, 0, psz, -1, buf, cb, NULL, NULL);
        *ppsz = buf;
        return S_OK;
    }
    return E_OUTOFMEMORY;
}

static PWSTR DupStrFromAnsi(PSTR psz)
{
    SIZE_T cch = MultiByteToWideChar(CP_ACP, 0, psz, -1, NULL, 0);
    PWSTR buf = (PWSTR)CoTaskMemAlloc(cch * sizeof(WCHAR));
    if (buf)
        MultiByteToWideChar(CP_ACP, 0, psz, -1, buf, cch);
    return buf;
}

static BOOL CanVariantChange(UINT vt)
{
    vt &= ~(VT_BYREF | VT_ARRAY);
    return (vt <= VT_UINT && vt != 15) || vt == VT_RECORD;
}

static HRESULT PROPVAR_FromFILETIME(PROPVARIANT *ppvd, FILETIME *pft, VARTYPE vt)
{
    ULARGE_INTEGER uli;
    uli.LowPart = pft->dwLowDateTime;
    uli.HighPart = pft->dwHighDateTime;
    ULONGLONG ft = uli.QuadPart, maxval = 0;
    switch (vt)
    {
        case VT_R4:
            if (ft > 0x7fffffffffffffffULL)
                return DISP_E_OVERFLOW;
            return SetPV(ppvd, vt, fltVal, (float)(LONGLONG)ft);
        case VT_R8:
            if (ft > 0x7fffffffffffffffULL)
                return DISP_E_OVERFLOW;
            return SetPV(ppvd, vt, dblVal, (double)(LONGLONG)ft);
        case VT_BOOL:
            return SetPV(ppvd, vt, boolVal, ft ? VARIANT_TRUE : VARIANT_FALSE);
        case VT_I1:
            maxval = 0x7f;
            break;
        case VT_UI1:
            maxval = 0xff;
            break;
        case VT_I2:
            maxval = 0x7fff;
            break;
        case VT_UI2:
            maxval = 0xffff;
            break;
        case VT_I4:
            maxval = 0x7fffffff;
            break;
        case VT_UI4:
            maxval = 0xffffffff;
            break;
        case VT_INT:
            maxval = INT_MAX;
            break;
        case VT_UINT:
            maxval = UINT_MAX;
            break;
        case VT_I8:
            maxval = 0x7fffffffffffffffULL;
            break;
        case VT_UI8:
            return SetPV(ppvd, vt, uhVal.QuadPart, ft);
    }
    if (maxval)
    {
        if (ft > maxval)
            return DISP_E_OVERFLOW;
        return SetPV(ppvd, vt, uhVal.QuadPart, ft); // Only valid for little-endian
    }
    return DISP_E_TYPEMISMATCH;
}

static HRESULT WINAPI PsPropVariantChangeTypeFallback(
  _Out_ PROPVARIANT *ppvd,
  _In_ PROPVARIANT *ppvs,
  _In_ WORD flags,
  _In_ VARTYPE vt)
{
    const LCID lcid = LOCALE_USER_DEFAULT;
    HRESULT hr = DISP_E_TYPEMISMATCH;
    PROPVARIANT pv;

    pv.vt = VT_EMPTY;

    if (ppvs->vt == VT_LPSTR)
    {
        LPWSTR str = DupStrFromAnsi(ppvs->pszVal);
        if (str)
        {
            if (vt == VT_LPWSTR)
                return SetPV(ppvd, VT_LPWSTR, pwszVal, str); // Avoid extra alloc/copy
            SetPV(&pv, VT_LPWSTR, pwszVal, str);
            hr = Ole32PropVariantChangeType(ppvd, &pv, lcid, flags, vt);
            CoTaskMemFree(str);
            return hr;
        }
    }

    if (ppvs->vt == VT_LPWSTR)
    {
        BSTR bs = PropSysAllocString(ppvs->pwszVal);
        if (bs)
        {
            if (vt == VT_BSTR)
                return SetPV(ppvd, VT_BSTR, bstrVal, bs); // Avoid extra alloc/copy
            SetPV(&pv, VT_BSTR, bstrVal, bs);
            hr = Ole32PropVariantChangeType(ppvd, &pv, lcid, flags, vt);
            PropSysFreeString(bs);
            return hr;
        }
    }

    switch (vt)
    {
        case VT_LPSTR:
        {
            hr = VChangeTypeEx((VARIANT*)&pv, (VARIANT*)ppvs, lcid, flags, VT_BSTR);
            if (SUCCEEDED(hr))
            {
                hr = DupStrToAnsi(pv.bstrVal, &ppvd->pszVal);
                PropVariantClear(&pv);
            }
            goto handled;
        }
        case VT_LPWSTR:
        {
            hr = VChangeTypeEx((VARIANT*)&pv, (VARIANT*)ppvs, lcid, flags, VT_BSTR);
            if (SUCCEEDED(hr))
            {
                hr = DupStrToWide(pv.bstrVal, &ppvd->pwszVal);
                PropVariantClear(&pv);
            }
            goto handled;
        }
    }

handled:
    if (SUCCEEDED(hr))
    {
        ppvd->vt = vt;
    }
    return hr;
}

HRESULT WINAPI Ole32PropVariantChangeType(
  _Out_ PROPVARIANT *ppvd,
  _In_ PROPVARIANT *ppvs,
  _In_ LCID lcid,
  _In_ WORD flags,
  _In_ VARTYPE vt)
{
    static PSPROPVARIANTCHANGETYPEPROC fnpvct = NULL;
    HRESULT hr;

    if (!ppvd || !ppvs)
        return E_INVALIDARG;

    if (vt == ppvs->vt)
        return PropVariantCopy(ppvd, ppvs);

    if (CanVariantChange(vt) && CanVariantChange(ppvs->vt))
    {
        hr = VChangeTypeEx((VARIANT*)ppvd, (VARIANT*)ppvs, lcid, flags, vt);
        if (SUCCEEDED(hr))
            return hr;
    }

    if (!fnpvct)
    {
        fnpvct = (PSPROPVARIANTCHANGETYPEPROC) GetSysLibProc("PROPSYS", "PropVariantChangeType");
        if (!fnpvct)
            fnpvct = PsPropVariantChangeTypeFallback;
    }
    hr = fnpvct(ppvd, ppvs, flags, vt);
    if (FAILED(hr))
    {
        if (ppvs->vt == VT_FILETIME) // Our PROPSYS::PropVariantChangeType does not handle this correctly
            hr = PROPVAR_FromFILETIME(ppvd, &ppvs->filetime, vt);
    }
    return hr;
}

/***********************************************************************
 *          PropVariantChangeType [OLE32.@]
 */
HRESULT WINAPI PropVariantChangeType(
  _Out_ PROPVARIANT *ppvd,
  _In_ PROPVARIANT *ppvs,
  _In_ LCID lcid,
  _In_ WORD flags,
  _In_ VARTYPE vt)
{
    return Ole32PropVariantChangeType(ppvd, ppvs, lcid, flags, vt);
}

#endif /* __REACTOS__ */
