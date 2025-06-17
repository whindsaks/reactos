/*
 * PROJECT:     NT Object Namespace shell extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     System Registry folder class implementation
 * COPYRIGHT:   Copyright 2015-2017 David Quintana <gigaherz@gmail.com>
 */

#include "precomp.h"

EXTERN_C INT WINAPI SHFormatDateTimeW(const FILETIME UNALIGNED *fileTime, DWORD *flags, LPWSTR buf, UINT size);
EXTERN_C INT WINAPIV ShellMessageBoxWrapW(HINSTANCE, HWND, LPCWSTR, LPCWSTR, UINT, ...);

// {1C6D6E08-2332-4A7B-A94D-6432DB2B5AE6}
const GUID CLSID_RegistryFolder = { 0x1c6d6e08, 0x2332, 0x4a7b, { 0xa9, 0x4d, 0x64, 0x32, 0xdb, 0x2b, 0x5a, 0xe6 } };

// {18A4B504-F6D8-4D8A-8661-6296514C2CF0}
//static const GUID GUID_RegistryColumns = { 0x18a4b504, 0xf6d8, 0x4d8a, { 0x86, 0x61, 0x62, 0x96, 0x51, 0x4c, 0x2c, 0xf0 } };

enum RegistryColumns
{
    REGISTRY_COLUMN_NAME = 0,
    REGISTRY_COLUMN_TYPE,
    REGISTRY_COLUMN_VALUE,
    REGISTRY_COLUMN_END
};

// -------------------------------
// CRegistryFolderExtractIcon
CRegistryFolderExtractIcon::CRegistryFolderExtractIcon() :
    m_pcidlFolder(NULL),
    m_pcidlChild(NULL)
{

}

CRegistryFolderExtractIcon::~CRegistryFolderExtractIcon()
{
    if (m_pcidlFolder)
        ILFree((LPITEMIDLIST)m_pcidlFolder);
    if (m_pcidlChild)
        ILFree((LPITEMIDLIST)m_pcidlChild);
}

HRESULT CRegistryFolderExtractIcon::Initialize(LPCWSTR ntPath, PCIDLIST_ABSOLUTE parent, UINT cidl, PCUITEMID_CHILD_ARRAY apidl)
{
    m_pcidlFolder = ILClone(parent);
    if (cidl != 1)
        return E_INVALIDARG;
    m_pcidlChild = ILClone(apidl[0]);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CRegistryFolderExtractIcon::GetIconLocation(
    UINT uFlags,
    LPWSTR szIconFile,
    UINT cchMax,
    INT *piIndex,
    UINT *pwFlags)
{
    const RegPidlEntry * entry = (RegPidlEntry *)m_pcidlChild;

    if ((entry->cb < sizeof(RegPidlEntry)) || (entry->magic != REGISTRY_PIDL_MAGIC))
        return E_INVALIDARG;

    UINT flags = 0;

    switch (entry->entryType)
    {
        case REG_ENTRY_KEY:
        case REG_ENTRY_ROOT:
            GetModuleFileNameW(g_hInstance, szIconFile, cchMax);
            *piIndex = -IDI_REGISTRYKEY;
            *pwFlags = flags;
            return S_OK;

        case REG_ENTRY_VALUE:
        case REG_ENTRY_VALUE_WITH_CONTENT:
            GetModuleFileNameW(g_hInstance, szIconFile, cchMax);
            *piIndex = -IDI_REGISTRYVALUE;
            *pwFlags = flags;
            return S_OK;

        default:
            GetModuleFileNameW(g_hInstance, szIconFile, cchMax);
            *piIndex = -IDI_NTOBJECTITEM;
            *pwFlags = flags;
            return S_OK;
    }
}

HRESULT STDMETHODCALLTYPE CRegistryFolderExtractIcon::Extract(
    LPCWSTR pszFile,
    UINT nIconIndex,
    HICON *phiconLarge,
    HICON *phiconSmall,
    UINT nIconSize)
{
    return SHDefExtractIconW(pszFile, nIconIndex, 0, phiconLarge, phiconSmall, nIconSize);
}

// CRegistryFolder

CRegistryFolder::CRegistryFolder()
{
}

CRegistryFolder::~CRegistryFolder()
{
}

// IShellFolder
HRESULT STDMETHODCALLTYPE CRegistryFolder::EnumObjects(
    HWND hwndOwner,
    SHCONTF grfFlags,
    IEnumIDList **ppenumIDList)
{
    if (m_NtPath[0] == 0 && m_hRoot == NULL)
    {
        return GetEnumRegistryRoot(ppenumIDList);
    }
    else
    {
        return GetEnumRegistryKey(m_NtPath, m_hRoot, ppenumIDList);
    }
}

HRESULT STDMETHODCALLTYPE CRegistryFolder::InternalBindToObject(
    PWSTR path,
    const RegPidlEntry * info,
    LPITEMIDLIST first,
    LPCITEMIDLIST rest,
    LPITEMIDLIST fullPidl,
    LPBC pbcReserved,
    IShellFolder** ppsfChild)
{
    if (wcslen(m_NtPath) == 0 && m_hRoot == NULL)
    {
        return ShellObjectCreatorInit<CRegistryFolder>(fullPidl, L"", info->rootKey, IID_PPV_ARG(IShellFolder, ppsfChild));
    }

    return ShellObjectCreatorInit<CRegistryFolder>(fullPidl, path, m_hRoot, IID_PPV_ARG(IShellFolder, ppsfChild));
}

HRESULT STDMETHODCALLTYPE CRegistryFolder::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    m_shellPidl = ILClone(pidl);
    m_hRoot = NULL;

    StringCbCopyW(m_NtPath, sizeof(m_NtPath), L"");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CRegistryFolder::Initialize(PCIDLIST_ABSOLUTE pidl, PCWSTR ntPath, HKEY hRoot)
{
    m_shellPidl = ILClone(pidl);
    m_hRoot = hRoot;

    StringCbCopyW(m_NtPath, sizeof(m_NtPath), ntPath);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CRegistryFolder::GetDefaultColumnState(
    UINT iColumn,
    SHCOLSTATEF *pcsFlags)
{
    switch (iColumn)
    {
        case REGISTRY_COLUMN_NAME:
            *pcsFlags = SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT;
            return S_OK;

        case REGISTRY_COLUMN_TYPE:
            *pcsFlags = SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT;
            return S_OK;

        case REGISTRY_COLUMN_VALUE:
            *pcsFlags = SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT | SHCOLSTATE_SLOW;
            return S_OK;
    }

    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE CRegistryFolder::GetDetailsEx(
    LPCITEMIDLIST pidl,
    const SHCOLUMNID *pscid,
    VARIANT *pv)
{
    const RegPidlEntry * info;

    TRACE("GetDetailsEx\n");

    if (pidl)
    {
        HRESULT hr = GetInfoFromPidl(pidl, &info);
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        static const GUID storage = PSGUID_STORAGE;
        if (IsEqualGUID(pscid->fmtid, storage))
        {
            if (pscid->pid == PID_STG_NAME)
            {
                if (info->entryNameLength > 0)
                {
                    return MakeVariantString(pv, info->entryName);
                }
                return  MakeVariantString(pv, IDS_REGDEFVAL);
            }
            else if (pscid->pid == PID_STG_STORAGETYPE)
            {
                if (info->entryType == REG_ENTRY_ROOT)
                {
                    return MakeVariantString(pv, L"Key");
                }

                if (info->entryType == REG_ENTRY_KEY)
                {
                    if (info->contentsLength > 0)
                    {
                        PWSTR td = (PWSTR)(((PBYTE)info) + FIELD_OFFSET(RegPidlEntry, entryName) + info->entryNameLength + sizeof(WCHAR));

                        return MakeVariantString(pv, td);
                    }
                    return MakeVariantString(pv, L"Key");
                }

                return MakeVariantString(pv, RegistryTypeNames[info->contentType]);
            }
            else if (pscid->pid == PID_STG_CONTENTS)
            {
                PCWSTR strValueContents;

                hr = FormatContentsForDisplay(info, m_hRoot, m_NtPath, &strValueContents);
                if (FAILED_UNEXPECTEDLY(hr))
                    return hr;

                if (hr == S_FALSE)
                {
                    V_VT(pv) = VT_EMPTY;
                    return S_OK;
                }

                hr = MakeVariantString(pv, strValueContents);

                CoTaskMemFree((PVOID)strValueContents);

                return hr;

            }
        }
    }

    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE CRegistryFolder::GetDetailsOf(
    LPCITEMIDLIST pidl,
    UINT iColumn,
    SHELLDETAILS *psd)
{
    const RegPidlEntry * info;

    TRACE("GetDetailsOf\n");

    if (pidl)
    {
        HRESULT hr = GetInfoFromPidl(pidl, &info);
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        switch (iColumn)
        {
            case REGISTRY_COLUMN_NAME:
            {
                psd->fmt = LVCFMT_LEFT;

                if (info->entryNameLength > 0)
                {
                    return MakeStrRetFromString(info->entryName, info->entryNameLength, &(psd->str));
                }
                return MakeStrRetFromString(IDS_REGDEFVAL, &(psd->str));
            }

            case REGISTRY_COLUMN_TYPE:
            {
                psd->fmt = LVCFMT_LEFT;

                if (info->entryType == REG_ENTRY_ROOT)
                {
                    return MakeStrRetFromString(L"Key", &(psd->str));
                }

                if (info->entryType == REG_ENTRY_KEY)
                {
                    if (info->contentsLength > 0)
                    {
                        PWSTR td = (PWSTR)(((PBYTE)info) + FIELD_OFFSET(RegPidlEntry, entryName) + info->entryNameLength + sizeof(WCHAR));

                        return MakeStrRetFromString(td, info->contentsLength, &(psd->str));
                    }

                    return MakeStrRetFromString(L"Key", &(psd->str));
                }

                return MakeStrRetFromString(RegistryTypeNames[info->contentType], &(psd->str));
            }

            case REGISTRY_COLUMN_VALUE:
            {
                psd->fmt = LVCFMT_LEFT;

                PCWSTR strValueContents;

                hr = FormatContentsForDisplay(info, m_hRoot, m_NtPath, &strValueContents);
                if (FAILED_UNEXPECTEDLY(hr))
                    return hr;

                if (hr == S_FALSE)
                {
                    return MakeStrRetFromString(L"(Empty)", &(psd->str));
                }

                hr = MakeStrRetFromString(strValueContents, &(psd->str));

                CoTaskMemFree((PVOID)strValueContents);

                return hr;
            }
        }
    }
    else
    {
        switch (iColumn)
        {
            case REGISTRY_COLUMN_NAME:
                psd->fmt = LVCFMT_LEFT;
                psd->cxChar = 30;

                // TODO: Make localizable
                MakeStrRetFromString(L"Name", &(psd->str));
                return S_OK;

            case REGISTRY_COLUMN_TYPE:
                psd->fmt = LVCFMT_LEFT;
                psd->cxChar = 20;

                // TODO: Make localizable
                MakeStrRetFromString(L"Type", &(psd->str));
                return S_OK;

            case REGISTRY_COLUMN_VALUE:
                psd->fmt = LVCFMT_LEFT;
                psd->cxChar = 20;

                // TODO: Make localizable
                MakeStrRetFromString(L"Value", &(psd->str));
                return S_OK;
        }
    }

    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE CRegistryFolder::MapColumnToSCID(
    UINT iColumn,
    SHCOLUMNID *pscid)
{
    static const GUID storage = PSGUID_STORAGE;
    switch (iColumn)
    {
        case REGISTRY_COLUMN_NAME:
            pscid->fmtid = storage;
            pscid->pid = PID_STG_NAME;
            return S_OK;

        case REGISTRY_COLUMN_TYPE:
            pscid->fmtid = storage;
            pscid->pid = PID_STG_STORAGETYPE;
            return S_OK;

        case REGISTRY_COLUMN_VALUE:
            pscid->fmtid = storage;
            pscid->pid = PID_STG_CONTENTS;
            return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT CRegistryFolder::CompareIDs(LPARAM lParam, const RegPidlEntry * first, const RegPidlEntry * second)
{
    HRESULT hr;

    DWORD sortMode = lParam & 0xFFFF0000;
    DWORD column = lParam & 0x0000FFFF;

    if (sortMode == SHCIDS_ALLFIELDS)
    {
        if (column != 0)
            return E_INVALIDARG;

        int minsize = min(first->cb, second->cb);
        hr = MAKE_COMPARE_HRESULT(memcmp(second, first, minsize));
        if (hr != S_EQUAL)
            return hr;

        return MAKE_COMPARE_HRESULT(second->cb - first->cb);
    }

    switch (column)
    {
        case REGISTRY_COLUMN_NAME:
            return CompareName(lParam, first, second);

        case REGISTRY_COLUMN_TYPE:
        {
            if (first->entryType != second->entryType)
                return MAKE_COMPARE_HRESULT(second->entryType - first->entryType);

            if (first->entryType == REG_ENTRY_KEY)
            {
                if (first->contentsLength == 0 || second->contentsLength == 0)
                    return (first->contentsLength == 0) ? S_GREATERTHAN : S_LESSTHAN;

                PWSTR firstKey = (PWSTR)(((PBYTE)first) + FIELD_OFFSET(RegPidlEntry, entryName) + first->entryNameLength + sizeof(WCHAR));
                PWSTR secondKey = (PWSTR)(((PBYTE)second) + FIELD_OFFSET(RegPidlEntry, entryName) + second->entryNameLength + sizeof(WCHAR));
                return MAKE_COMPARE_HRESULT(lstrcmpW(firstKey, secondKey));
            }

            return CompareName(lParam, first, second);
        }

        case REGISTRY_COLUMN_VALUE:
        {
            PCWSTR firstContent, secondContent;

            if (FAILED_UNEXPECTEDLY(FormatContentsForDisplay(first, m_hRoot, m_NtPath, &firstContent)))
                return E_INVALIDARG;

            if (FAILED_UNEXPECTEDLY(FormatContentsForDisplay(second, m_hRoot, m_NtPath, &secondContent)))
                return E_INVALIDARG;

            hr = MAKE_COMPARE_HRESULT(lstrcmpW(firstContent, secondContent));

            CoTaskMemFree((LPVOID)firstContent);
            CoTaskMemFree((LPVOID)secondContent);

            return hr;
        }
    }

    DbgPrint("Unsupported sorting mode.\n");
    return E_INVALIDARG;
}

ULONG CRegistryFolder::ConvertAttributes(const RegPidlEntry * entry, PULONG inMask)
{
    ULONG mask = inMask ? *inMask : 0xFFFFFFFF;
    ULONG flags = 0;

    if (IsFolder(entry))
        flags |= SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_BROWSABLE;

    // FIXME: We want to add these but can't until CDefaultContextMenu is fixed so we can handle DFM_CMD
    // if (entry->entryType != REG_ENTRY_ROOT)
    //     flags |= SFGAO_CANDELETE | SFGAO_HASPROPSHEET;
    return flags & mask;
}

BOOL CRegistryFolder::IsFolder(const RegPidlEntry * info)
{
    return (info->entryType == REG_ENTRY_KEY) || (info->entryType == REG_ENTRY_ROOT);
}

HRESULT CRegistryFolder::GetInfoFromPidl(LPCITEMIDLIST pcidl, const RegPidlEntry ** pentry)
{
    if (!pcidl)
    {
        DbgPrint("PCIDL is NULL\n");
        return E_INVALIDARG;
    }

    RegPidlEntry * entry = (RegPidlEntry*) &(pcidl->mkid);
    if (entry->cb < sizeof(RegPidlEntry))
    {
        DbgPrint("PCIDL too small %l (required %l)\n", entry->cb, sizeof(RegPidlEntry));
        return E_INVALIDARG;
    }

    if (entry->magic != REGISTRY_PIDL_MAGIC)
    {
        DbgPrint("PCIDL magic mismatch %04x (expected %04x)\n", entry->magic, REGISTRY_PIDL_MAGIC);
        return E_INVALIDARG;
    }

    *pentry = entry;
    return S_OK;
}

HRESULT CRegistryFolder::FormatValueData(DWORD contentType, PVOID td, DWORD contentsLength, PCWSTR * strContents)
{
    switch (contentType)
    {
        case REG_NONE:
        {
            PCWSTR strTodo = L"";
            DWORD bufferLength = (wcslen(strTodo) + 1) * sizeof(WCHAR);
            PWSTR strValue = (PWSTR)CoTaskMemAlloc(bufferLength);
            StringCbCopyW(strValue, bufferLength, strTodo);
            *strContents = strValue;
            return S_OK;
        }

        case REG_SZ:
        case REG_EXPAND_SZ:
        {
            PWSTR strValue = (PWSTR)CoTaskMemAlloc(contentsLength + sizeof(WCHAR));
            StringCbCopyNW(strValue, contentsLength + sizeof(WCHAR), (LPCWSTR)td, contentsLength);
            *strContents = strValue;
            return S_OK;
        }

        case REG_MULTI_SZ:
        {
            PCWSTR separator = L" "; // To match regedit
            size_t sepChars = wcslen(separator);
            int strings = 0;
            int stringChars = 0;

            PCWSTR strData = (PCWSTR)td;
            while (*strData)
            {
                size_t len = wcslen(strData);
                stringChars += len;
                strData += len + 1; // Skips null-terminator
                strings++;
            }

            int cch = stringChars + (strings - 1) * sepChars + 1;

            PWSTR strValue = (PWSTR)CoTaskMemAlloc(cch * sizeof(WCHAR));

            strValue[0] = 0;

            strData = (PCWSTR)td;
            while (*strData)
            {
                StrCatW(strValue, strData);
                strData += wcslen(strData) + 1;
                if (*strData)
                    StrCatW(strValue, separator);
            }

            *strContents = strValue;
            return S_OK;
        }

        case REG_DWORD:
        {
            DWORD bufferLength = 64 * sizeof(WCHAR);
            PWSTR strValue = (PWSTR)CoTaskMemAlloc(bufferLength);
            StringCbPrintfW(strValue, bufferLength, L"0x%08x (%d)",
                *(DWORD*)td, *(DWORD*)td);
            *strContents = strValue;
            return S_OK;
        }

        case REG_QWORD:
        {
            DWORD bufferLength = 64 * sizeof(WCHAR);
            PWSTR strValue = (PWSTR)CoTaskMemAlloc(bufferLength);
            StringCbPrintfW(strValue, bufferLength, L"0x%016llx (%lld)",
                *(LARGE_INTEGER*)td, ((LARGE_INTEGER*)td)->QuadPart);
            *strContents = strValue;
            return S_OK;
        }

        case REG_BINARY:
        {
            DWORD bufferLength = (contentsLength * 3 + 1) * sizeof(WCHAR);
            PWSTR strValue = (PWSTR)CoTaskMemAlloc(bufferLength);
            PWSTR strTemp = strValue;
            PBYTE data = (PBYTE)td;
            for (DWORD i = 0; i < contentsLength; i++)
            {
                StringCbPrintfW(strTemp, bufferLength, L"%02x ", data[i]);
                strTemp += 3;
                bufferLength -= 3;
            }
            *strContents = strValue;
            return S_OK;
        }

        default:
        {
            PCWSTR strFormat = L"<Unimplemented value type %d>";
            DWORD bufferLength = (wcslen(strFormat) + 15) * sizeof(WCHAR);
            PWSTR strValue = (PWSTR)CoTaskMemAlloc(bufferLength);
            StringCbPrintfW(strValue, bufferLength, strFormat, contentType);
            *strContents = strValue;
            return S_OK;
        }
    }
}

HRESULT CRegistryFolder::FormatContentsForDisplay(const RegPidlEntry * info, HKEY rootKey, LPCWSTR ntPath, PCWSTR * strContents)
{
    PVOID td = (((PBYTE)info) + FIELD_OFFSET(RegPidlEntry, entryName) + info->entryNameLength + sizeof(WCHAR));

    if (info->entryType == REG_ENTRY_VALUE_WITH_CONTENT)
    {
        if (info->contentsLength > 0)
        {
            return FormatValueData(info->contentType, td, info->contentsLength, strContents);
        }
    }
    else if (info->entryType == REG_ENTRY_VALUE)
    {
        PVOID valueData;
        DWORD valueLength;
        HRESULT hr = ReadRegistryValue(rootKey, ntPath, info->entryName, &valueData, &valueLength);
        if (FAILED_UNEXPECTEDLY(hr))
        {
            PCWSTR strEmpty = L"(Error reading value)";
            DWORD bufferLength = (wcslen(strEmpty) + 1) * sizeof(WCHAR);
            PWSTR strValue = (PWSTR)CoTaskMemAlloc(bufferLength);
            StringCbCopyW(strValue, bufferLength, strEmpty);
            *strContents = strValue;
            return S_OK;
        }

        if (valueLength > 0)
        {
            hr = FormatValueData(info->contentType, valueData, valueLength, strContents);

            CoTaskMemFree(valueData);

            return hr;
        }
    }
    else
    {
        PCWSTR strEmpty = L"";
        DWORD bufferLength = (wcslen(strEmpty) + 1) * sizeof(WCHAR);
        PWSTR strValue = (PWSTR)CoTaskMemAlloc(bufferLength);
        StringCbCopyW(strValue, bufferLength, strEmpty);
        *strContents = strValue;
        return S_OK;
    }

    PCWSTR strEmpty = L"(Empty)";
    DWORD bufferLength = (wcslen(strEmpty) + 1) * sizeof(WCHAR);
    PWSTR strValue = (PWSTR)CoTaskMemAlloc(bufferLength);
    StringCbCopyW(strValue, bufferLength, strEmpty);
    *strContents = strValue;
    return S_OK;
}

struct RegItemInfo
{
    HKEY hRootKey;
    PCWSTR pszPath, pszName;
    RegPidlEntry *pEntry;

    HRESULT Initialize(PCWSTR ParentPath, CDataObjectHIDA &cida)
    {
        pszPath = ParentPath + (*ParentPath == L'\\');
        if (!cida)
            return cida.hr();
        pEntry = (RegPidlEntry*)HIDA_GetPIDLItem(cida, 0);
        pszName = pEntry->entryNameLength ? pEntry->entryName : NULL;
        PCUIDLIST_ABSOLUTE pidlFolder = HIDA_GetPIDLFolder(cida);
        for (LPCITEMIDLIST pidl = pidlFolder; !ILIsEmpty(pidl); pidl = ILGetNext(pidl))
        {
            RegPidlEntry *pKey = (RegPidlEntry*)pidl;
            if (pKey->magic != REGISTRY_PIDL_MAGIC || pKey->entryType != REG_ENTRY_ROOT)
                continue;
            hRootKey = pKey->rootKey;
            return S_OK;
        }
        return E_UNEXPECTED;
    }
};

HRESULT CRegistryFolder::DeleteKeyOrValue(HWND hWnd, IDataObject *pDO)
{
    HKEY hKey;
    CDataObjectHIDA cida(pDO);
    RegItemInfo reg;
    HRESULT hr = reg.Initialize(m_NtPath, cida);
    if (SUCCEEDED(hr) && SUCCEEDED(hr = OpenRegKey(reg.hRootKey, reg.pszPath, KEY_SET_VALUE, hKey)))
    {
        PCWSTR pszDispName = reg.pszName;
        WCHAR szName[42];
        if (StrIsNullOrEmpty(pszDispName))
             LoadString(IDS_REGDEFVAL, const_cast<PWSTR>(pszDispName = szName), _countof(szName));

        UINT res = ERROR_INVALID_PARAMETER, event = 0;
        int response = ShellMessageBoxWrapW(g_hInstance, hWnd, IDS_CONFIRMDELETE, pszDispName,
                                            MB_YESNO | MB_ICONQUESTION, pszDispName);
        if (response != IDYES)
        {
            res = ERROR_SUCCESS;
        }
        else if (!IsFolder(reg.pEntry))
        {
            res = RegDeleteValueW(hKey, reg.pszName);
            event = SHCNE_DELETE;
        }
        else if (!StrIsNullOrEmpty(reg.pszName))
        {
            res = SHDeleteKeyW(hKey, reg.pszName);
            event = SHCNE_RMDIR;
        }
        RegCloseKey(hKey);
        hr = HRESULT_FROM_WIN32(res);

        if (SUCCEEDED(hr) && event)
        {
            if (LPITEMIDLIST pidl = ILCombine(HIDA_GetPIDLFolder(cida), HIDA_GetPIDLItem(cida, 0)))
            {
                SHChangeNotify(event, SHCNF_IDLIST, pidl, NULL);
                ILFree(pidl);
            }
        }
    }
    return SUCCEEDED(hr) ? hr : SHELL_ErrorBox(hWnd, hr);
}

HRESULT CRegistryFolder::DisplayItemProperties(HWND hWnd, IDataObject *pDO)
{
    HKEY hKey, hSubKey;
    CDataObjectHIDA cida(pDO);
    RegItemInfo reg;
    HRESULT hr = reg.Initialize(m_NtPath, cida);
    if (SUCCEEDED(hr) && SUCCEEDED(hr = OpenRegKey(reg.hRootKey, reg.pszPath, KEY_QUERY_VALUE, hKey)))
    {
        PCWSTR pszDispName = reg.pszName;
        WCHAR szName[42];
        if (StrIsNullOrEmpty(pszDispName))
             LoadString(IDS_REGDEFVAL, const_cast<PWSTR>(pszDispName = szName), _countof(szName));

        UINT res;
        if (!IsFolder(reg.pEntry))
        {
            DWORD dwSize = 0;
            res = RegQueryValueExW(hKey, reg.pszName, NULL, NULL, NULL, &dwSize);
            if (res == ERROR_SUCCESS)
            {
                WCHAR buf[150];
                ShortSizeFormatW(dwSize, buf);
                MessageBoxW(hWnd, buf, pszDispName, MB_ICONINFORMATION);
            }
        }
        else
        {
            res = RegOpenKeyExW(hKey, reg.pszName, 0, KEY_QUERY_VALUE, &hSubKey);
            if (res == ERROR_SUCCESS)
            {
                FILETIME ft;
                res = RegQueryInfoKeyW(hSubKey, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, &ft);
                if (res == ERROR_SUCCESS)
                {
                    WCHAR szDate[150];
                    SHFormatDateTimeW(&ft, NULL, szDate, _countof(szDate));
                    MessageBoxW(hWnd, szDate, pszDispName, MB_ICONINFORMATION);
                }
                RegCloseKey(hSubKey);
            }
        }
        RegCloseKey(hKey);
        hr = HRESULT_FROM_WIN32(res);
    }
    return SUCCEEDED(hr) ? hr : SHELL_ErrorBox(hWnd, hr);
}

static BOOL AppendMenuItem(UINT &idFinal, QCMINFO &qcmi, UINT idRelative, PCWSTR ResId, BOOL Disabled = FALSE)
{
    WCHAR szString[100];
    LoadString(ResId, szString, _countof(szString));
    UINT mf = MF_BYPOSITION | (Disabled ? MF_GRAYED : 0) | (ResId ? MF_STRING : MF_SEPARATOR);
    UINT id = idRelative ? (qcmi.idCmdFirst + idRelative) : idRelative;
    BOOL succ = id <= qcmi.idCmdLast && InsertMenuW(qcmi.hmenu, qcmi.indexMenu, mf, id, szString);
    if (succ)
    {
        qcmi.indexMenu++;
        idFinal = id + 1;
    }
    return succ;
}

HRESULT CALLBACK CRegistryFolder::DefCtxMenuCallback(IShellFolder *pSF, HWND hwnd, IDataObject *pDO, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    enum {
        IDM_DELETE = 1,
        IDM_PROPERTIES,
    };
    CRegistryFolder *pThis = static_cast<CRegistryFolder*>(pSF);
    switch (uMsg)
    {
        case DFM_MERGECONTEXTMENU:
        {
            QCMINFO &qcmi = *(QCMINFO*)lParam;
            CDataObjectHIDA cida(pDO);
            if (!cida || cida->cidl == 0)
                return S_OK;
            RegPidlEntry *item = (RegPidlEntry*)HIDA_GetPIDLItem(cida, 0);
            if (item->entryType == REG_ENTRY_ROOT)
                return S_OK;

            BOOL disable = cida->cidl != 1 ? MF_GRAYED : 0;
            UINT orgIndex = qcmi.indexMenu, idFinal = 0;

            if (item->entryType == REG_ENTRY_KEY)
                AppendMenuItem(idFinal, qcmi, 0, NULL, FALSE);
            AppendMenuItem(idFinal, qcmi, IDM_DELETE, IDS_MENUDELETE, disable);
            AppendMenuItem(idFinal, qcmi, 0, NULL, FALSE);
            AppendMenuItem(idFinal, qcmi, IDM_PROPERTIES, IDS_MENUPROPERTIES, disable);

            if (idFinal > qcmi.idCmdFirst)
                qcmi.idCmdFirst = idFinal;
            qcmi.indexMenu = orgIndex;
            return S_OK;
        }

        case DFM_INVOKECOMMAND:
            switch (wParam)
            {
                case DFM_CMD_DELETE:
                case IDM_DELETE:
                    return pThis->DeleteKeyOrValue(hwnd, pDO);
                case DFM_CMD_PROPERTIES:
                case IDM_PROPERTIES:
                    return pThis->DisplayItemProperties(hwnd, pDO);
            }
            return S_FALSE;
    }
    return E_NOTIMPL;
}
