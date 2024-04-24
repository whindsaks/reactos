/* TODO file header */

#define PROPSYS_NOTYET 1
#include "precomp.h"
#include <ntquery.h>

EXTERN_C const DECLSPEC_SELECTANY SHCOLUMNID PKEY_ItemTypeText = { PSGUID_STORAGE, PID_STG_STORAGETYPE };
EXTERN_C const DECLSPEC_SELECTANY SHCOLUMNID PKEY_FindData = { PSGUID_SHELLDETAILS, PID_FINDDATA };
EXTERN_C const DECLSPEC_SELECTANY SHCOLUMNID PKEY_DescriptionID = { PSGUID_SHELLDETAILS, PID_DESCRIPTIONID };

EXTERN_C HRESULT SHELL_InitVariantFromBuffer(const void *p, UINT cb, VARIANT *v)
{
    SAFEARRAY *psa = SafeArrayCreateVector(VT_UI1, 0, cb);
    if (psa) 
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
        memcpy(p, V_UNION(v, parray)->pvData, cb);
        return S_OK;
    }
    return E_FAIL;
}
