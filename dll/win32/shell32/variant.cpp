/*
 * PROJECT:     shell32
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VARIANT/PROPVARIANT functions for shell without propsys.dll
 * COPYRIGHT:   Whindmar Saksit (whindsaks@proton.me)
 */

#define SHELL32PROPSYS -1
#include "precomp.h"
#include <ntquery.h>

EXTERN_C const DECLSPEC_SELECTANY SHCOLUMNID PKEY_ItemNameDisplay = { PSGUID_STORAGE, PID_STG_NAME };
EXTERN_C const DECLSPEC_SELECTANY SHCOLUMNID PKEY_Size = { PSGUID_STORAGE, PID_STG_SIZE };
EXTERN_C const DECLSPEC_SELECTANY SHCOLUMNID PKEY_ItemTypeText = { PSGUID_STORAGE, PID_STG_STORAGETYPE };
EXTERN_C const DECLSPEC_SELECTANY SHCOLUMNID PKEY_DateModified = { PSGUID_STORAGE, PID_STG_WRITETIME };
EXTERN_C const DECLSPEC_SELECTANY SHCOLUMNID PKEY_FileAttributes = { PSGUID_STORAGE, PID_STG_ATTRIBUTES };
EXTERN_C const DECLSPEC_SELECTANY SHCOLUMNID PKEY_FindData = { PSGUID_SHELLDETAILS, PID_FINDDATA };
EXTERN_C const DECLSPEC_SELECTANY SHCOLUMNID PKEY_DescriptionID = { PSGUID_SHELLDETAILS, PID_DESCRIPTIONID };

static inline HRESULT SetVariantBSTR(BSTR bstr, VARIANT *v)
{
    V_VT(v) = VT_BSTR;
    V_BSTR(v) = bstr;
    return S_OK;
}

EXTERN_C HRESULT SHELL_InitVariantFromBuffer(const void *p, UINT cb, VARIANT *v)
{
    if (SAFEARRAY *psa = SafeArrayCreateVector(VT_UI1, 0, cb))
    {
        CopyMemory(psa->pvData, p, cb);
        V_VT(v) = VT_ARRAY | VT_UI1;
        V_UNION(v, parray) = psa;
        return S_OK;
    }
    V_VT(v) = VT_EMPTY;
    return E_OUTOFMEMORY;
}

EXTERN_C HRESULT SHELL_VariantToBuffer(const VARIANT *v, void *p, UINT cb)
{
    if (v && V_VT(v) == (VT_ARRAY | VT_UI1))
    {
        CopyMemory(p, V_UNION(v, parray)->pvData, cb);
        return S_OK;
    }
    return E_FAIL;
}

EXTERN_C HRESULT SHELL_InitVariantFromStrRet(STRRET *pStr, PCUITEMID_CHILD pidl, VARIANT *v)
{
    BSTR bstr;
    HRESULT hr = StrRetToBSTR(pStr, pidl, &bstr);
    return SUCCEEDED(hr) ? SetVariantBSTR(bstr, v) : hr;
}
