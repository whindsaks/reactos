/*
 * PROJECT:     ole32 apitest
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     [Prop]Variant functions
 * COPYRIGHT:   Copyright 2026 Whindmar Saksit <whindsaks@proton.me>
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <stdio.h>
#include <wine/test.h>

#include <windows.h>
#include <oleauto.h>

#define test_S_OK(hres, message) ok((hres) == S_OK, "%s (0x%lx instead of S_OK)\n", (message), (hres))
#define ALLOCCHECK(mem) ( (mem) ? (TRUE) : (skip("OOM"), FALSE) )

typedef void (WINAPI *PROPSYSFREESTRING)(BSTR);
PROPSYSFREESTRING fnPropSysFreeString;
typedef BSTR (WINAPI *PROPSYSALLOCSTRING)(PCWSTR);
PROPSYSALLOCSTRING fnPropSysAllocString;
typedef HRESULT (WINAPI *PROPVARIANTCLEAR)(PROPVARIANT*);
PROPVARIANTCLEAR fnPropVariantClear;
#define PropVariantClear fnPropVariantClear
typedef HRESULT (WINAPI *PROPVARIANTCOPY)(PROPVARIANT*,const PROPVARIANT*);
PROPVARIANTCOPY fnPropVariantCopy;
#define PropVariantCopy fnPropVariantCopy
typedef HRESULT (WINAPI *OLE32PROPVARIANTCHANGETYPE)(PROPVARIANT*,PROPVARIANT*,LCID,WORD,VARTYPE);
OLE32PROPVARIANTCHANGETYPE fnPropVariantChangeType;
#define PropVariantChangeType fnPropVariantChangeType

typedef struct {
    DWORD Size;
    WCHAR Buffer[MAX_PATH];
} FAKEBSTR;

static PROPVARIANT* Init(PROPVARIANT &pv, VARTYPE vt, UINT64 val = 0)
{
    pv.vt = vt;
    pv.uhVal.QuadPart = val;
    return &pv;
}

static PROPVARIANT* Init(PROPVARIANT &pv, FAKEBSTR &fbs, PCWSTR str)
{
    fbs.Size = wsprintfW(fbs.Buffer, L"%s", str) * sizeof(WCHAR);
    return Init(pv, VT_BSTR, (SIZE_T)(fbs.Buffer));
}

static HRESULT PVCT(PROPVARIANT*d, PROPVARIANT*s, VARTYPE vt, WORD f = 0, LCID l = LOCALE_USER_DEFAULT)
{
    return fnPropVariantChangeType(d, s, l, f, vt);
}

static void test_PropSysAllocString()
{
    BSTR bs;

    bs = fnPropSysAllocString(L"Test");
    if (ALLOCCHECK(bs))
    {
        ok_long(bs[1], 'e');
        ok_long(bs[-2], 4 * sizeof(WCHAR));
        fnPropSysFreeString(bs);
    }

    ok((fnPropSysFreeString(NULL), TRUE), "PropSysFreeString NULL no crash");
    ok(fnPropSysAllocString(NULL) == NULL, "PropSysAllocString NULL");
}

static void test_PropVariantClear()
{
    PROPVARIANT pv;

    test_S_OK(PropVariantClear(Init(pv, VT_EMPTY, 0)), "Clear VT_EMPTY");
    test_S_OK(PropVariantClear(Init(pv, VT_NULL, 0)), "Clear VT_NULL");
    test_S_OK(PropVariantClear(Init(pv, VT_I4, 42)), "Clear VT_I4");
    test_S_OK(PropVariantClear(Init(pv, VT_UI8, 42)), "Clear VT_UI8");
    test_S_OK(PropVariantClear(Init(pv, VT_UINT, 42)), "Clear VT_UINT");
    ok_long(PropVariantClear(Init(pv, VT_INT_PTR, 42)), STG_E_INVALIDPARAMETER);
    test_S_OK(PropVariantClear(Init(pv, VT_FILETIME, 42)), "Clear VT_FILETIME");
    test_S_OK(PropVariantClear(NULL), "Clear NULL");
    test_S_OK(PropVariantClear(Init(pv, VT_BSTR, 0)), "Clear NULL VT_BSTR");

    PWSTR pszw = (PWSTR)CoTaskMemAlloc(sizeof(L"Test"));
    if (ALLOCCHECK(pszw))
    {
        lstrcpyW(pszw, L"Test");
        test_S_OK(PropVariantClear(Init(pv, VT_LPWSTR, (SIZE_T)pszw)), "Clear VT_LPWSTR");
        ok(pv.vt != VT_LPWSTR, "Clear VT_LPWSTR VT\n");
    }

    GUID* pGuid = (GUID*)CoTaskMemAlloc(sizeof(GUID));
    if (ALLOCCHECK(pGuid))
    {
        // Note: We don't actually care what the GUID contains
        test_S_OK(PropVariantClear(Init(pv, VT_CLSID, (SIZE_T)pGuid)), "Clear VT_CLSID");
        ok(pv.vt != VT_CLSID, "Clear VT_CLSID VT\n");
    }
}

static void test_PropVariantCopy()
{
    PROPVARIANT s, d;
    d.vt = VT_EMPTY;

    Init(s, VT_EMPTY, 0);
    ok(!PropVariantCopy(&d, &s) && d.vt == VT_EMPTY, "Copy VT_EMPTY\n");
    Init(s, VT_NULL, 0);
    ok(!PropVariantCopy(&d, &s) && d.vt == VT_NULL, "Copy VT_NULL\n");
    Init(s, VT_UI8, 42);
    ok(!PropVariantCopy(&d, &s) && d.vt == VT_UI8 && d.lVal == 42, "Copy VT_UI8\n");
    Init(s, VT_UINT, 42);
    ok(!PropVariantCopy(&d, &s) && d.vt == VT_UINT && d.lVal == 42, "Copy VT_UINT\n");
    Init(s, VT_INT_PTR, 42);
    ok(FAILED(PropVariantCopy(&d, &s)), "Copy VT_INT_PTR\n");
    Init(s, VT_FILETIME, 42);
    ok(!PropVariantCopy(&d, &s) && d.vt == VT_FILETIME && d.lVal == 42, "Copy VT_FILETIME\n");
    Init(s, VT_BSTR, 0);
    ok(!PropVariantCopy(&d, &s) && d.vt == VT_BSTR && !d.bstrVal, "Copy NULL VT_BSTR\n");

    GUID guid = { 42 };
    Init(s, VT_CLSID, (SIZE_T)(&guid)), d.vt = VT_EMPTY;
    ok(!PropVariantCopy(&d, &s) && guid == *d.puuid, "Copy VT_CLSID\n");
    PropVariantClear(&d);
}

static void test_PropVariantChangeType()
{
    PROPVARIANT s, d;

    d.vt = VT_EMPTY;
    ok(!(PVCT(&d, Init(s, VT_EMPTY), VT_NULL)), "VT_EMPTY==>VT_NULL\n");
    d.vt = VT_EMPTY;
    ok(FAILED(PVCT(&d, Init(s, VT_NULL), VT_EMPTY)), "VT_NULL==>VT_EMPTY\n");
    d.vt = VT_EMPTY;
    ok(!PVCT(&d, Init(s, VT_I2, LOWORD(-42)), VT_I4) && d.lVal == -42, "VT_I2==>VT_I4\n");
    ok(!PVCT(&d, Init(s, VT_I2, (SHORT)-42), VT_I8) && d.lVal == -42, "VT_I2==>VT_I8\n");
    d.vt = VT_EMPTY;
    ok(FAILED(PVCT(&d, Init(s, VT_I4, 0x12345678), VT_I2)), "VT_I4==>VT_I2 overflow\n");
    d.vt = VT_EMPTY;
    ok(!PVCT(&d, Init(s, VT_INT, 0x12345678), VT_I4), "VT_INT==>VT_I4\n");
    d.vt = VT_EMPTY;
    ok(FAILED(PVCT(&d, Init(s, VT_INT, 0x12345678), VT_I2)), "VT_INT==>VT_I2 overflow\n");
    d.vt = VT_EMPTY;
    ok(!PVCT(&d, Init(s, VT_INT, -42), VT_I8) && d.lVal == -42, "VT_INT==>VT_I8\n");
    ok_long(PVCT(NULL, NULL, VT_I4), E_INVALIDARG);

    ok(FAILED(PVCT(Init(d, VT_EMPTY), Init(s, VT_FILETIME, 42), VT_BSTR)), "VT_FILETIME==>VT_BSTR\n");
    PropVariantClear(&d);
    ok(FAILED(PVCT(Init(d, VT_EMPTY), Init(s, VT_FILETIME, 42), VT_LPWSTR)), "VT_FILETIME==>VT_LPWSTR\n");
    PropVariantClear(&d);
    d.vt = VT_EMPTY;
    ok(!PVCT(&d, Init(s, VT_FILETIME, 42), VT_I4) && d.lVal == 42, "VT_FILETIME==>VT_I4\n");
    d.vt = VT_EMPTY;
    ok(!PVCT(&d, Init(s, VT_FILETIME, 0x7fffffffffffffffULL+0), VT_I8), "VT_FILETIME==>VT_I8\n");
    d.vt = VT_EMPTY;
    ok(FAILED(PVCT(&d, Init(s, VT_FILETIME, 0x7fffffffffffffffULL+1), VT_I8)), "VT_FILETIME==>VT_I8 overflow\n");
    d.vt = VT_EMPTY;
    ok(!PVCT(&d, Init(s, VT_FILETIME, 0xffffffffffffffffULL), VT_UI8), "VT_FILETIME==>VT_UI8\n");
    d.vt = VT_EMPTY;
    ok(!PVCT(&d, Init(s, VT_FILETIME, 42), VT_INT) && d.lVal == 42, "VT_FILETIME==>VT_INT\n");
    d.vt = VT_EMPTY;
    ok(FAILED(PVCT(&d, Init(s, VT_FILETIME, (UINT)INT_MAX + 1), VT_INT)), "VT_FILETIME==>VT_INT overflow\n");

    FAKEBSTR fbs;
    ok(!PVCT(Init(d, VT_EMPTY), Init(s, fbs, L"Test"), VT_LPWSTR) && d.pwszVal[1] == 'e', "VT_BSTR==>VT_LPWSTR\n");
    PropVariantClear(&d);
    ok(!PVCT(Init(d, VT_EMPTY), Init(s, fbs, L"Test"), VT_LPSTR) && d.pszVal[1] == 'e', "VT_BSTR==>VT_LPSTR\n");
    PropVariantClear(&d);
    Init(s, fbs, L"Test")->vt = VT_LPWSTR;
    ok(!PVCT(Init(d, VT_EMPTY), &s, VT_LPSTR) && d.pszVal[1] == 'e', "VT_LPWSTR==>VT_LPSTR\n");
    PropVariantClear(&d);
    Init(s, fbs, L"Test")->vt = VT_LPWSTR;
    ok(!PVCT(Init(d, VT_EMPTY), &s, VT_BSTR) && d.bstrVal[1] == 'e', "VT_LPWSTR==>VT_BSTR\n");
    PropVariantClear(&d);
    Init(s, fbs, L"")->vt = VT_LPSTR, lstrcpyA((char*)(s.bstrVal), "Test");
    ok(!PVCT(Init(d, VT_EMPTY), &s, VT_BSTR) && d.bstrVal[1] == 'e', "VT_LPSTR==>VT_BSTR\n");
    PropVariantClear(&d);
}

START_TEST(propvariant)
{
    HMODULE ole32 = LoadLibraryA("ole32.dll");
    fnPropSysFreeString = (PROPSYSFREESTRING)GetProcAddress(ole32, "PropSysFreeString");
    fnPropSysAllocString = (PROPSYSALLOCSTRING)GetProcAddress(ole32, "PropSysAllocString");
    fnPropVariantClear = (PROPVARIANTCLEAR)GetProcAddress(ole32, "PropVariantClear");
    fnPropVariantCopy = (PROPVARIANTCOPY)GetProcAddress(ole32, "PropVariantCopy");
    fnPropVariantChangeType = (OLE32PROPVARIANTCHANGETYPE)GetProcAddress(ole32, "PropVariantChangeType");

    if (fnPropSysFreeString && fnPropSysAllocString)
        test_PropSysAllocString();
    else
        skip("PropSysAllocString\n");

    if (fnPropVariantClear)
        test_PropVariantClear();
    else
        skip("PropVariantClear\n");

    if (fnPropVariantCopy)
        test_PropVariantCopy();
    else
        skip("PropVariantCopy\n");

    if (fnPropVariantChangeType)
        test_PropVariantChangeType();
    else
        skip("PropVariantChangeType\n");
}
