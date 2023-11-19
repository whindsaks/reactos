/*
 * IQueryAssociations object and helper functions
 *
 * Copyright 2002 Jon Griffiths
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

WINE_DEFAULT_DEBUG_CHANNEL(shell);

#define ASSOCF_REJECTEMPTY 0x80000000
#define INTERNAL_FLAGS (ASSOCF_REJECTEMPTY)
#define SUPPORTED_INIT_FLAGS (ASSOCF_INIT_NOREMAPCLSID | ASSOCF_INIT_FOR_FILE | \
                              ASSOCF_INIT_DEFAULTTOSTAR | ASSOCF_INIT_DEFAULTTOFOLDER)
#define SUPPORTED_GET_FLAGS (ASSOCF_IGNOREBASECLASS | ASSOCF_NOTRUNCATE)

#define Assoc_NTVer() ( DLL_EXPORT_VERSION )

static void Assoc_Free(const void*p)
{
    HeapFree(GetProcessHeap(), 0, const_cast<void*>(p));
}

static LPVOID Assoc_Alloc(SIZE_T cb)
{
    return HeapAlloc(GetProcessHeap(), 0, cb);
}

static HRESULT Assoc_Create(IQueryAssociations**ppv)
{
    return SHCoCreateInstance(NULL, &CLSID_QueryAssociations, NULL, IID_PPV_ARG(IQueryAssociations, ppv));
}

static HRESULT SHELL32_GetDefaultNoAssociationIcon(LPWSTR Out, UINT cch)
{
    // Note: Not using -IDI_SHELL_DOCUMENT to avoid the ExtractIcon -1 issue.
    return StringCchPrintfW(Out, cch, L"%s,%d", swShell32Name, 0);
}

static HRESULT SHELL32_GetDefaultFileTypeString(LPWSTR Ext, LPWSTR Out, SIZE_T cch)
{
    BOOL folder = Ext == NULL;
    UINT resid = folder ? IDS_DIRECTORY : IDS_ANY_FILE;
    LPCWSTR fmt = folder ? L"Folder" : L"%s File";
    WCHAR fmtbuf[80];
    if (!folder)
    {
        Ext += *Ext == '.';
        _wcsupr(Ext); // Note: Modifies the callers buffer on purpose to avoid StrCpyNW
    }
    else
    {
        Ext = const_cast<LPWSTR>(fmt); // Any non-empty string
    }
    if (LoadStringW(shell32_hInstance, resid, fmtbuf, _countof(fmtbuf)))
    {
        fmt = fmtbuf;
    }
    HRESULT hr = StringCchPrintfW(Out, cch, fmt, Ext);
    if (!Ext[0] && SUCCEEDED(hr))
    {
        StrTrimW(Out, L" -"); // Handle names without extension: "name." => "%s-File" => "File"
    }
    return hr;
}

static HRESULT VerifyFilePath(HRESULT hrOriginal, ASSOCF Flags, LPCWSTR Path)
{
    if (SUCCEEDED(hrOriginal))
    {
        if (!*Path)
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if ((Flags & ASSOCF_VERIFY) && !PathFileExistsW(Path))
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    return hrOriginal;
}

static HRESULT GetValue(HKEY hKey, const WCHAR *subkey, const WCHAR *name, DWORD RRF, void **data)
{
    if (subkey && *subkey)
    {
        LONG ret = RegOpenKeyExW(hKey, subkey, 0, KEY_QUERY_VALUE, &hKey);
        if (ret)
            return HRESULT_FROM_WIN32(ret);
        HRESULT hr = GetValue(hKey, NULL, name, RRF, data);
        RegCloseKey(hKey);
        return hr;
    }

    for (UINT8 tries = 0; ++tries;)
    {
        DWORD size = 0;
        LONG ret = RegGetValueW(hKey, NULL, name, RRF, NULL, NULL, &size);
        if (ret != ERROR_SUCCESS || !data)
        {
            return HRESULT_FROM_WIN32(ret);
        }

        *data = Assoc_Alloc(size);
        if (!*data)
        {
            return E_OUTOFMEMORY;
        }
        ret = RegGetValueW(hKey, NULL, name, RRF, NULL, *data, &size);
        if (ret == ERROR_SUCCESS)
        {
            return size; // Return the data size on success
        }
        Assoc_Free(*data);
        if (ret != ERROR_MORE_DATA)
        {
            return HRESULT_FROM_WIN32(ret);
        }
    }
    return E_FAIL;
}

static HRESULT GetRegString(HKEY hKey, const WCHAR *subkey, const WCHAR *name, LPWSTR *ppStr)
{
    return GetValue(hKey, subkey, name, RRF_RT_REG_SZ, (void**)ppStr);
}

static HRESULT ReturnString(ASSOCF flags, LPWSTR out, DWORD *outlen, LPCWSTR data, DWORD datalen = -1)
{
    HRESULT hr = S_OK;
    DWORD len;

    TRACE("flags=0x%08x, data=%s\n", flags, debugstr_w(data));

    if (datalen == (DWORD)-1)
        datalen = strlenW(data) + 1;

    if (!out)
    {
        *outlen = datalen;
        return S_FALSE;
    }

    len = datalen;
    if (*outlen < datalen)
    {
        if (flags & ASSOCF_NOTRUNCATE)
        {
            len = 0;
            if (*outlen > 0)
                out[0] = UNICODE_NULL;
            hr = E_POINTER;
        }
        else
        {
            len = min(*outlen, datalen);
            if (len)
                out[--len] = UNICODE_NULL;
            hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
    }

    if (len)
    {
        memcpy(out, data, len*sizeof(WCHAR));
    }
    *outlen = datalen;
    return hr;
}

static HRESULT ReturnRegistryString(ASSOCF flags, HKEY hKey, const WCHAR *subkey, const WCHAR *name, LPWSTR out, DWORD *outlen)
{
    WCHAR *string;
    HRESULT hr = GetRegString(hKey, subkey, name, &string);
    if (SUCCEEDED(hr))
    {
        if (!*string && (flags & ASSOCF_REJECTEMPTY))
            hr = HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
        else
            hr = ReturnString(flags, out, outlen, string);
        Assoc_Free(string);
    }
    return hr;
}

/**************************************************************************
 *  IQueryAssociations
 *
 * DESCRIPTION
 *  This object provides a layer of abstraction over the system registry in
 *  order to simplify the process of parsing associations between files.
 *  Associations in this context means the registry entries that link (for
 *  example) the extension of a file with its description, list of
 *  applications to open the file with, and actions that can be performed on it
 *  (the shell displays such information in the context menu of explorer
 *  when you right-click on a file).
 *
 * HELPERS
 * You can use this object transparently by calling the helper functions
 * AssocQueryKeyA(), AssocQueryStringA() and AssocQueryStringByKeyA(). These
 * create an IQueryAssociations object, perform the requested actions
 * and then dispose of the object. Alternatively, you can create an instance
 * of the object using AssocCreate() and call the following methods on it:
 *
 * METHODS
 */

CQueryAssociations::CQueryAssociations() : hkeySource(0), hkeyProgID(0), m_pBase(0)
{
}

CQueryAssociations::~CQueryAssociations()
{
    Reset();
}

LPCWSTR CQueryAssociations::GetFileExt()
{
    if (m_InitType == '.')
        return GetInitString();
    if (m_InitFlags & (ASSOCF_INIT_DEFAULTTOSTAR | ASSOCF_INIT_FOR_FILE))
        return L".";
    return NULL;
}

IQueryAssociations* CQueryAssociations::GetBaseClass(ASSOCF flags)
{
    if (flags & ASSOCF_IGNOREBASECLASS)
    {
        return NULL;
    }
    if (!m_pBase && !(m_InitFlags & ASSOCF_IGNOREBASECLASS))
    {
        m_InitFlags |= ASSOCF_IGNOREBASECLASS; // Don't try this again

        ASSOCF initFlags = 0; // ASSOCF_INIT_DEFAULTTO* must be off by default
        LPCWSTR progid = NULL;
        WCHAR buf[MAX_PATH];
        if (!m_InitType) // We only want to check for the BaseClass value if we are a ProgId
        {
            DWORD cb = sizeof(buf);
            if (!RegGetValueW(hkeyProgID, NULL, L"BaseClass", RRF_RT_REG_SZ, NULL, buf, &cb) &&
                *buf != UNICODE_NULL)
            {
                progid = buf;
                initFlags |= m_InitFlags & (ASSOCF_INIT_DEFAULTTOFOLDER | ASSOCF_INIT_DEFAULTTOSTAR);
            }
        }
        if (!progid && (m_InitFlags & ASSOCF_INIT_DEFAULTTOFOLDER))
            progid = L"Folder";
        if (!progid && (m_InitFlags & ASSOCF_INIT_DEFAULTTOSTAR))
            progid = L"*";

        IQueryAssociations *pQA;
        if (progid && SUCCEEDED(Assoc_Create(&pQA)))
        {
            if (SUCCEEDED(pQA->Init(initFlags, progid, NULL, NULL)))
                IUnknown_Set((IUnknown**)&m_pBase, pQA);
            pQA->Release();
        }
    }
    return m_pBase;
}

/**************************************************************************
 *  IQueryAssociations_Init
 *
 * Initialise an IQueryAssociations object.
 *
 * PARAMS
 *  cfFlags    [I] ASSOCF_ flags from "shlwapi.h"
 *  pszAssoc   [I] String for the root key name, or NULL if hkeyProgid is given
 *  hkeyProgid [I] Handle for the root key, or NULL if pszAssoc is given
 *  hWnd       [I] Reserved, must be NULL.
 *
 * RETURNS
 *  Success: S_OK. iface is initialised with the parameters given.
 *  Failure: An HRESULT error code indicating the error.
 */
HRESULT STDMETHODCALLTYPE CQueryAssociations::Init(
    ASSOCF cfFlags,
    LPCWSTR pszAssoc,
    HKEY hkeyProgid,
    HWND hWnd)
{
    const ASSOCF unimplemented_flags = ~(SUPPORTED_INIT_FLAGS | ASSOCF_IGNOREBASECLASS);
    TRACE("(%p)->(%d,%s,%p,%p)\n", this,
                                    cfFlags,
                                    debugstr_w(pszAssoc),
                                    hkeyProgid,
                                    hWnd);

    if (hWnd != NULL)
    {
        FIXME("hwnd != NULL not supported\n");
    }

    if (cfFlags & unimplemented_flags)
    {
        FIXME("unsupported flags: %x\n", cfFlags & unimplemented_flags);
    }
    ASSERT(!(cfFlags & INTERNAL_FLAGS));
    cfFlags &= ~INTERNAL_FLAGS;

    Reset();
    m_InitFlags = cfFlags;
    if (pszAssoc != NULL)
    {
        WCHAR *progId;
        HRESULT hr;

        StrCpyNW(m_Init, pszAssoc, MAX_PATH);
        if (*pszAssoc == '.' || *pszAssoc == '{')
        {
            m_InitType = *pszAssoc;
        }

        LONG ret = RegOpenKeyExW(HKEY_CLASSES_ROOT, pszAssoc, 0, KEY_READ, &hkeySource);
        if (ret)
        {
            return S_OK;
        }

        /* if this is a progid */
        if (!m_InitType)
        {
            this->hkeyProgID = this->hkeySource;
            return S_OK;
        }

        /* if it's not a progid, it's a file extension or clsid */
        if (*pszAssoc == '.')
        {
            /* for a file extension, the progid is the default value */
            hr = GetRegString(this->hkeySource, NULL, NULL, &progId);
            if (FAILED(hr))
                return S_OK;
        }
        else /* if (*pszAssoc == '{') */
        {
            if (m_InitFlags & ASSOCF_INIT_NOREMAPCLSID)
            {
                this->hkeyProgID = this->hkeySource; // TODO: Should hkeyProgID be NULL?
                return S_OK;
            }
            /* for a clsid, the progid is the default value of the ProgID subkey */
            hr = GetRegString(this->hkeySource, L"ProgID", NULL, &progId);
            if (FAILED(hr))
                return S_OK;
        }

        /* open the actual progid key, the one with the shell subkey */
        ret = RegOpenKeyExW(HKEY_CLASSES_ROOT,
                            progId,
                            0,
                            KEY_READ,
                            &this->hkeyProgID);
        Assoc_Free(progId);
        return S_OK;
    }
    else if (hkeyProgid != NULL)
    {
        /* reopen the key so we don't end up closing a key owned by the caller */
        RegOpenKeyExW(hkeyProgid, NULL, 0, KEY_READ, &this->hkeyProgID);
        this->hkeySource = this->hkeyProgID;
        return S_OK;
    }
    else
        return E_INVALIDARG;
}

/**************************************************************************
 *  IQueryAssociations_GetString
 *
 * Get a file association string from the registry.
 *
 * PARAMS
 *  cfFlags  [I]   ASSOCF_ flags from "shlwapi.h"
 *  str      [I]   Type of string to get (ASSOCSTR enum from "shlwapi.h")
 *  pszExtra [I]   Extra information about the string location
 *  pszOut   [O]   Destination for the association string
 *  pcchOut  [I/O] Length of pszOut
 *
 * RETURNS
 *  Success: S_OK. pszOut contains the string, pcchOut contains its length.
 *  Failure: An HRESULT error code indicating the error.
 */
HRESULT STDMETHODCALLTYPE CQueryAssociations::GetString(
    ASSOCF flags,
    ASSOCSTR str,
    LPCWSTR pszExtra,
    LPWSTR pszOut,
    DWORD *pcchOut)
{
    const ASSOCF unimplemented_flags = ~(SUPPORTED_GET_FLAGS);
    DWORD len = 0;
    WCHAR path[MAX_PATH];

    TRACE("(%p)->(0x%08x, %u, %s, %p, %p)\n", this, flags, str, debugstr_w(pszExtra), pszOut, pcchOut);
    if (flags & (unimplemented_flags & ~SUPPORTED_INIT_FLAGS))
    {
        FIXME("%08x: unimplemented flags\n", flags & unimplemented_flags);
    }
    ASSERT(!(flags & INTERNAL_FLAGS));
    flags &= ~INTERNAL_FLAGS;

    if (!pcchOut)
    {
        return E_UNEXPECTED;
    }

    HRESULT hr = E_INVALIDARG;
    if (!HasKey())
    {
        BOOL allow = (str == ASSOCSTR_FRIENDLYDOCNAME && GetFileExt()) ||
                     (str == ASSOCSTR_DEFAULTICON && GetFileExt() && !(m_InitFlags & ASSOCF_INIT_IGNOREUNKNOWN));
        if (!allow)
        {
            return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
        }
    }

    switch (str)
    {
        case ASSOCSTR_COMMAND:
        {
            WCHAR *command;
            hr = this->GetCommand(pszExtra, &command);
            if (SUCCEEDED(hr))
            {
                hr = ReturnString(flags, pszOut, pcchOut, command);
                Assoc_Free(command);
            }
            break;
        }
        case ASSOCSTR_EXECUTABLE:
        {
            hr = this->GetExecutable(pszExtra, path, MAX_PATH, &len);
            if (SUCCEEDED(hr))
                hr = ReturnString(flags, pszOut, pcchOut, path, ++len);
            break;
        }
        case ASSOCSTR_FRIENDLYDOCNAME:
        {
            WCHAR buf[MAX_PATH + 80];
            LONG ret;
            // FriendlyTypeName in ProgId
            ret = RegLoadMUIStringW(hkeyProgID, L"FriendlyTypeName",
                                    buf, _countof(buf), NULL, 0, NULL);
            hr = HRESULT_FROM_WIN32(ret);
            // Default value in ProgId
            if (FAILED(hr))
            {
                len = _countof(buf);
                hr = ReturnRegistryString(flags | ASSOCF_REJECTEMPTY,
                                          hkeyProgID, NULL, NULL, buf, &len);
            }
            // FriendlyTypeName in .Ext (only check if there is no ProgId)
            if (FAILED(hr) && hkeySource != hkeyProgID)
            {
                ret = RegLoadMUIStringW(hkeySource, L"FriendlyTypeName",
                                        buf, _countof(buf), NULL, 0, NULL);
                hr = HRESULT_FROM_WIN32(ret);
            }
            // Use the ProgId itself
            if (FAILED(hr) && hkeySource != hkeyProgID)
            {
                len = _countof(buf);
                hr = ReturnRegistryString(flags | ASSOCF_REJECTEMPTY,
                                          hkeySource, NULL, NULL, buf, &len);
            }
            // Generate a name if we know the extension
            if (FAILED(hr))
            {
                BOOL folder = (m_InitFlags & ASSOCF_INIT_DEFAULTTOFOLDER); // NT5 only checks the flag, NT6 also checks if it's not an extension
                if ((GetFileExt() || folder) && 
                    SUCCEEDED(SHELL32_GetDefaultFileTypeString(folder ? NULL : GetInitString(),
                                                               buf, _countof(buf))))
                {
                    hr = S_OK; // Only changed on success to maintain registry error from above
                }
            }
            // Finished
            if (SUCCEEDED(hr))
            {
                hr = ReturnString(flags, pszOut, pcchOut, buf);
            }
            break;
        }
        case ASSOCSTR_FRIENDLYAPPNAME:
        {
            PVOID verinfoW = NULL;
            DWORD size, retval = 0;
            UINT flen;
            WCHAR *bufW;
            WCHAR fileDescW[41];

            hr = this->GetExecutable(pszExtra, path, MAX_PATH, &len);
            if (FAILED(hr))
            {
               break;
            }
            retval = GetFileVersionInfoSizeW(path, &size);
            if (!retval)
            {
                goto get_friendly_name_fail;
            }
            verinfoW = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, retval);
            if (!verinfoW)
            {
                return E_OUTOFMEMORY;
            }
            if (!GetFileVersionInfoW(path, 0, retval, verinfoW))
            {
                goto get_friendly_name_fail;
            }
            if (VerQueryValueW(verinfoW, L"\\VarFileInfo\\Translation", (LPVOID *)&bufW, &flen))
            {
                UINT i;
                DWORD *langCodeDesc = (DWORD *)bufW;
                for (i = 0; i < flen / sizeof(DWORD); i++)
                {
                    sprintfW(fileDescW, L"\\StringFileInfo\\%04x%04x\\FileDescription",
                             LOWORD(langCodeDesc[i]), HIWORD(langCodeDesc[i]));
                    if (VerQueryValueW(verinfoW, fileDescW, (LPVOID *)&bufW, &flen))
                    {
                        /* Does strlenW(bufW) == 0 mean we use the filename? */
                        TRACE("found FileDescription: %s\n", debugstr_w(bufW));
                        hr = ReturnString(flags, pszOut, pcchOut, bufW);
                        HeapFree(GetProcessHeap(), 0, verinfoW);
                        break;
                    }
                }
            }
        get_friendly_name_fail:
            PathRemoveExtensionW(path);
            PathStripPathW(path);
            TRACE("using filename: %s\n", debugstr_w(path));
            hr = ReturnString(flags, pszOut, pcchOut, path);
            HeapFree(GetProcessHeap(), 0, verinfoW);
            break;
        }
        case ASSOCSTR_CONTENTTYPE:
        {
            if (!hkeyProgID && Assoc_NTVer() < 0x600)
                break;
            hr = ReturnRegistryString(flags, hkeySource, NULL, L"Content Type",
                                      pszOut, pcchOut);
            break;
        }
        case ASSOCSTR_DEFAULTICON:
        {
            // ProgId Icon > ProgId App > .Ext Icon > .Ext App > NoAssoc
            struct DefaultIcon
            {
                static HRESULT Get(CQueryAssociations*that, UINT Flags,
                                   HKEY hKey, LPWSTR pOut, DWORD *pcch)
                {
                    HRESULT hr = ReturnRegistryString(Flags | ASSOCF_REJECTEMPTY, hKey,
                                                      L"DefaultIcon", NULL, pOut, pcch);
                    if (FAILED(hr) && hKey && Assoc_NTVer() >= 0x600)
                    {
                        WCHAR buf[MAX_PATH + 42];
                        DWORD len;
                        // Windows forces the open verb here, perhaps for performance reasons.
                        hr = that->GetExecutable(L"open", buf, _countof(buf), &len);
                        if (SUCCEEDED(hr = VerifyFilePath(hr, Flags, buf)))
                            hr = ReturnString(Flags, pOut, pcch, buf);
                    }
                    return hr;
                }
            };
            hr = DefaultIcon::Get(this, flags, this->hkeyProgID, pszOut, pcchOut);
            if (FAILED(hr) && GetFileExt())
            {
                if (!this->hkeyProgID) // Only try .Ext if there is no ProgId
                {
                    hr = DefaultIcon::Get(this, flags, this->hkeySource, pszOut, pcchOut);
                }
                if (FAILED(hr) && !(m_InitFlags & ASSOCF_INIT_IGNOREUNKNOWN))
                {
                    WCHAR buf[MAX_PATH + 42];
                    hr = SHELL32_GetDefaultNoAssociationIcon(buf, _countof(buf));
                    if (SUCCEEDED(hr))
                    {
                        hr = ReturnString(flags, pszOut, pcchOut, buf);
                    }
                }
            }
            break;
        }
        case ASSOCSTR_SHELLEXTENSION:
        {
            WCHAR keypath[ARRAY_SIZE(L"ShellEx\\") + 39], guid[39];
            CLSID clsid;

            hr = CLSIDFromString(pszExtra, &clsid);
            if (FAILED(hr))
            {
                break;
            }
            strcpyW(keypath, L"ShellEx\\");
            strcatW(keypath, pszExtra);
            DWORD size = sizeof(guid);
            LONG ret = RegGetValueW(this->hkeySource, keypath, NULL, RRF_RT_REG_SZ, NULL, guid, &size);
            hr = ret
                ? HRESULT_FROM_WIN32(ret)
                : ReturnString(flags, pszOut, pcchOut, guid, size / sizeof(WCHAR));
            break;
        }
        default:
        {
            ASSERT(hr == E_INVALIDARG);
            FIXME("assocstr %d unimplemented!\n", str);
        }
    }

    if (FAILED(hr))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && Assoc_NTVer() >= 0x600)
            hr = HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);

        if (IQueryAssociations *pBase = GetBaseClass(flags))
            hr = pBase->GetString(flags, str, pszExtra, pszOut, pcchOut);
    }
    return hr;
}

/**************************************************************************
 *  IQueryAssociations_GetKey
 *
 * Get a file association key from the registry.
 *
 * PARAMS
 *  cfFlags  [I] ASSOCF_ flags from "shlwapi.h"
 *  assockey [I] Type of key to get (ASSOCKEY enum from "shlwapi.h")
 *  pszExtra [I] Extra information about the key location
 *  phkeyOut [O] Destination for the association key
 *
 * RETURNS
 *  Success: S_OK. phkeyOut contains a handle to the key.
 *  Failure: An HRESULT error code indicating the error.
 */
HRESULT STDMETHODCALLTYPE CQueryAssociations::GetKey(
    ASSOCF cfFlags,
    ASSOCKEY assockey,
    LPCWSTR pszExtra,
    HKEY *phkeyOut)
{
    FIXME("(%p,0x%8x,0x%8x,%s,%p)-stub!\n", this, cfFlags, assockey,
            debugstr_w(pszExtra), phkeyOut);
    return E_NOTIMPL;
}

/**************************************************************************
 *  IQueryAssociations_GetData
 *
 * Get the data for a file association key from the registry.
 *
 * PARAMS
 *  cfFlags   [I]   ASSOCF_ flags from "shlwapi.h"
 *  assocdata [I]   Type of data to get (ASSOCDATA enum from "shlwapi.h")
 *  pszExtra  [I]   Extra information about the data location
 *  pvOut     [O]   Destination for the association key
 *  pcbOut    [I/O] Size of pvOut
 *
 * RETURNS
 *  Success: S_OK. pszOut contains the data, pcbOut contains its length.
 *  Failure: An HRESULT error code indicating the error.
 */
static HRESULT GetDataHelper(ASSOCF flags, HKEY hKey, LPCWSTR pszExtra, LPVOID pvOut, DWORD *pcbOut)
{
    const DWORD maxout = *pcbOut, rrf = RRF_RT_ANY | RRF_NOEXPAND;
    if (hKey)
    {
        LONG err = RegGetValueW(hKey, NULL, pszExtra, rrf, NULL, pvOut, pcbOut);
        if (err == ERROR_MORE_DATA)
        {
            if (flags & ASSOCF_NOTRUNCATE)
                return E_POINTER;

            if (pvOut)
            {
                void *data;
                HRESULT hr = GetValue(hKey, NULL, pszExtra, rrf, &data);
                if (SUCCEEDED(hr))
                {
                    CopyMemory(pvOut, data, min((UINT)hr, maxout));
                    Assoc_Free(data);
                    *pcbOut = (UINT)hr; // pcbOut indicates the full size.
                    return S_OK; // Windows returns S_OK even though the data was truncated!
                }
            }
        }
        return HRESULT_FROM_WIN32(err);
    }
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

HRESULT STDMETHODCALLTYPE CQueryAssociations::GetData(ASSOCF cfFlags, ASSOCDATA assocdata, LPCWSTR pszExtra, LPVOID pvOut, DWORD *pcbOut)
{
    TRACE("(%p,0x%8x,0x%8x,%s,%p,%p)\n", this, cfFlags, assocdata,
            debugstr_w(pszExtra), pvOut, pcbOut);

    const ASSOCF unimplemented_flags = ~(SUPPORTED_GET_FLAGS);
    if (cfFlags & (unimplemented_flags & ~SUPPORTED_INIT_FLAGS))
    {
        FIXME("Unsupported flags: %x\n", cfFlags & unimplemented_flags);
    }
    ASSERT(!(cfFlags & INTERNAL_FLAGS));
    cfFlags &= ~INTERNAL_FLAGS;

    HRESULT hr = ERROR_FILE_NOT_FOUND;
    HKEY hKey = hkeyProgID ? hkeyProgID : hkeySource;
    if (!HasKey())
    {
        goto base;
    }

    switch(assocdata)
    {
        case ASSOCDATA_EDITFLAGS:
        {
            hr = GetDataHelper(cfFlags, hKey, L"EditFlags", pvOut, pcbOut);
            break;
        }
        case ASSOCDATA_VALUE:
        {
            hr = GetDataHelper(cfFlags, hKey, pszExtra, pvOut, pcbOut);
            break;
        }
        default:
        {
            hr = E_INVALIDARG;
            FIXME("Unsupported ASSOCDATA value: %d\n", assocdata);
        }
    }

    if (FAILED(hr)) base:
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && Assoc_NTVer() >= 0x600)
            hr = HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);

        if (IQueryAssociations *pBase = GetBaseClass(cfFlags))
            hr = pBase->GetData(cfFlags, assocdata, pszExtra, pvOut, pcbOut);
    }
    return hr;
}

/**************************************************************************
 *  IQueryAssociations_GetEnum
 *
 * Not yet implemented in native Win32.
 *
 * PARAMS
 *  cfFlags   [I] ASSOCF_ flags from "shlwapi.h"
 *  assocenum [I] Type of enum to get (ASSOCENUM enum from "shlwapi.h")
 *  pszExtra  [I] Extra information about the enum location
 *  riid      [I] REFIID to look for
 *  ppvOut    [O] Destination for the interface.
 *
 * RETURNS
 *  Success: S_OK.
 *  Failure: An HRESULT error code indicating the error.
 *
 * NOTES
 *  Presumably this function returns an enumerator object.
 */
HRESULT STDMETHODCALLTYPE CQueryAssociations::GetEnum(
    ASSOCF cfFlags,
    ASSOCENUM assocenum,
    LPCWSTR pszExtra,
    REFIID riid,
    LPVOID *ppvOut)
{
    return E_NOTIMPL;
}

HRESULT CQueryAssociations::GetValue(HKEY hkey, const WCHAR *name, void **data, DWORD *data_size)
{
    ASSERT(data);
    void *value;
    HRESULT hr = ::GetValue(hkey, NULL, name, RRF_RT_ANY | RRF_NOEXPAND, &value);
    if (SUCCEEDED(hr))
    {
        if (hr == 0) // Reject empty values
        {
            Assoc_Free(value);
            return E_FAIL;
        }
        *data = value;
        if(data_size)
            *data_size = hr;
        hr = S_OK;
    }
    return hr;
}

HRESULT CQueryAssociations::GetCommand(const WCHAR *extra, WCHAR **command)
{
    HKEY hkeyCommand;
    HKEY hkeyShell;
    HKEY hkeyVerb;
    HRESULT hr;
    LONG ret;
    WCHAR *extra_from_reg = NULL;
    WCHAR *filetype;

    /* When looking for file extension it's possible to have a default value
     that points to another key that contains 'shell/<verb>/command' subtree. */
    hr = this->GetValue(this->hkeySource, NULL, (void**)&filetype, NULL);
    if (hr == S_OK)
    {
        HKEY hkeyFile;

        ret = RegOpenKeyExW(HKEY_CLASSES_ROOT, filetype, 0, KEY_READ, &hkeyFile);
        Assoc_Free(filetype);

        if (ret == ERROR_SUCCESS)
        {
            ret = RegOpenKeyExW(hkeyFile, L"shell", 0, KEY_READ, &hkeyShell);
            RegCloseKey(hkeyFile);
        }
        else
        {
            ret = RegOpenKeyExW(this->hkeySource, L"shell", 0, KEY_READ, &hkeyShell);
        }
    }
    else
    {
        ret = RegOpenKeyExW(this->hkeySource, L"shell", 0, KEY_READ, &hkeyShell);
    }

    if (ret)
    {
        return HRESULT_FROM_WIN32(ret);
    }

    if (!extra)
    {
        /* check for default verb */
        hr = this->GetValue(hkeyShell, NULL, (void**)&extra_from_reg, NULL);
        if (FAILED(hr))
        {
            /* no default verb, try first subkey */
            DWORD max_subkey_len;

            ret = RegQueryInfoKeyW(hkeyShell, NULL, NULL, NULL, NULL, &max_subkey_len, NULL, NULL, NULL, NULL, NULL, NULL);
            if (ret)
            {
                RegCloseKey(hkeyShell);
                return HRESULT_FROM_WIN32(ret);
            }

            max_subkey_len++;
            extra_from_reg = static_cast<WCHAR*>(Assoc_Alloc(max_subkey_len * sizeof(WCHAR)));
            if (!extra_from_reg)
            {
                RegCloseKey(hkeyShell);
                return E_OUTOFMEMORY;
            }

            ret = RegEnumKeyExW(hkeyShell, 0, extra_from_reg, &max_subkey_len, NULL, NULL, NULL, NULL);
            if (ret)
            {
                Assoc_Free(extra_from_reg);
                RegCloseKey(hkeyShell);
                return HRESULT_FROM_WIN32(ret);
            }
        }
        extra = extra_from_reg;
    }

    /* open verb subkey */
    ret = RegOpenKeyExW(hkeyShell, extra, 0, KEY_READ, &hkeyVerb);
    Assoc_Free(extra_from_reg);
    RegCloseKey(hkeyShell);
    if (ret)
    {
        return HRESULT_FROM_WIN32(ret);
    }
    /* open command subkey */
    ret = RegOpenKeyExW(hkeyVerb, L"command", 0, KEY_READ, &hkeyCommand);
    RegCloseKey(hkeyVerb);
    if (ret)
    {
        return HRESULT_FROM_WIN32(ret);
    }
    hr = this->GetValue(hkeyCommand, NULL, (void**)command, NULL);
    RegCloseKey(hkeyCommand);
    return hr;
}

HRESULT CQueryAssociations::GetExecutable(LPCWSTR pszExtra, LPWSTR path, DWORD pathlen, DWORD *len)
{
    WCHAR *pszCommand;
    WCHAR *pszStart;
    WCHAR *pszEnd;

    HRESULT hr = this->GetCommand(pszExtra, &pszCommand);
    if (FAILED(hr))
    {
        return hr;
    }

    DWORD expLen = ExpandEnvironmentStringsW(pszCommand, NULL, 0);
    if (expLen > 0)
    {
        expLen++;
        WCHAR *buf = static_cast<WCHAR *>(Assoc_Alloc(expLen * sizeof(WCHAR)));
        ExpandEnvironmentStringsW(pszCommand, buf, expLen);
        Assoc_Free(pszCommand);
        pszCommand = buf;
    }

    /* cleanup pszCommand */
    if (pszCommand[0] == '"')
    {
        pszStart = pszCommand + 1;
        pszEnd = strchrW(pszStart, '"');
        if (pszEnd)
        {
            *pszEnd = 0;
        }
        *len = SearchPathW(NULL, pszStart, NULL, pathlen, path, NULL);
    }
    else
    {
        pszStart = pszCommand;
        for (pszEnd = pszStart; (pszEnd = strchrW(pszEnd, ' ')); pszEnd++)
        {
            WCHAR c = *pszEnd;
            *pszEnd = 0;
            if ((*len = SearchPathW(NULL, pszStart, NULL, pathlen, path, NULL)))
            {
                break;
            }
            *pszEnd = c;
        }
        if (!pszEnd)
        {
            *len = SearchPathW(NULL, pszStart, NULL, pathlen, path, NULL);
        }
    }

    Assoc_Free(pszCommand);
    if (!*len)
    {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    return S_OK;
}

// This class is faster than SHLWAPI::AssocQuery* because it avoids COM
struct ScopedQueryAssociations : public CQueryAssociations
{
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) { return E_NOINTERFACE; }
    virtual ULONG STDMETHODCALLTYPE AddRef() { return 1; }
    virtual ULONG STDMETHODCALLTYPE Release() { return 1; }
};

EXTERN_C HRESULT SHELL32_AssocQString(UINT Flags, UINT Str, LPCWSTR Assoc, LPCWSTR Extra, LPWSTR Out, DWORD cch)
{
    ScopedQueryAssociations qa;
    HRESULT hr = qa.Init(Flags, Assoc, NULL, NULL);
    if (SUCCEEDED(hr))
        hr = qa.GetString(Flags, (ASSOCSTR)Str, Extra, Out, &cch);
    return hr;
}

EXTERN_C HRESULT SHELL32_GetFileTypeString(LPCWSTR Ext, LPWSTR Out, UINT cchOut)
{
    BOOL folder = !Ext;
    ASSOCF init = folder ? ASSOCF_INIT_DEFAULTTOFOLDER : ASSOCF_INIT_FOR_FILE;
    return SHELL32_AssocQString(init | ASSOCF_IGNOREBASECLASS, ASSOCSTR_FRIENDLYDOCNAME,
                                folder ? L"Directory" : Ext, NULL, Out, cchOut);
}
