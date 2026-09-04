/*
 * PROJECT:     ReactOS MSC Shell Extension
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shell extension implementation
 * COPYRIGHT:   Copyright 2026 Whindmar Saksit <whindsaks@proton.me>
 */

#include "precomp.h"
#include <atlbase.h>
#include <initguid.h>
#include <msxml2.h>

DEFINE_GUID(CLSID_MscExtractIcon, 0x7A80E4A8,0x8005,0x11D2,0xBC,0xF8,0x00,0xC0,0x4F,0x72,0xC7,0x17);
#define CLSIDSTR "{7A80E4A8-8005-11D2-BCF8-00C04F72C717}"

CComModule g_Module;
HINSTANCE g_hInstance;

class CMscExtractIcon :
    public CComCoClass<CMscExtractIcon, &CLSID_MscExtractIcon>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IExtractIconW,
    public IPersistFile
{
protected:
    PWSTR m_File = NULL;

public:
    CMscExtractIcon()
    {
    }

    ~CMscExtractIcon()
    {
        SHFree(m_File);
    }

    HRESULT GetIconLocationFromMsc(PWSTR pszIconFile, UINT cchMax, int *piIndex);

    HRESULT GetDefaultIconLocation(PWSTR pszIconFile, UINT cchMax, int *piIndex)
    {
        *piIndex = 0;
        WCHAR szDir[MAX_PATH];
        GetSystemDirectory(szDir, _countof(szDir));
        return StringCchPrintfW(pszIconFile, cchMax, L"%s\\mmc.exe", szDir);
    }

    // IExtractIconW
    IFACEMETHODIMP Extract(PCWSTR pszFile, UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) override
    {
        return S_FALSE; // My paths are files, call SHExtractIconsW/SHDefExtractIconW for me.
    }

    IFACEMETHODIMP GetIconLocation(UINT GilIn, PWSTR pszIconFile, UINT cchMax, int *piIndex, UINT *GilOut) override
    {
        *GilOut = GIL_PERINSTANCE;

        if (GilIn & GIL_ASYNC)
            return E_PENDING;

        if ((GilIn & GIL_DEFAULTICON) || FAILED(GetIconLocationFromMsc(pszIconFile, cchMax, piIndex)))
            return GetDefaultIconLocation(pszIconFile, cchMax, piIndex);
        return S_OK;
    }

    // IPersistFile
    IFACEMETHODIMP GetClassID(CLSID *pClassID) override
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP IsDirty() override
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP Load(LPCOLESTR pszFileName, DWORD dwMode) override
    {
        SHFree(m_File);
        m_File = NULL;
        return SHStrDupW(pszFileName, &m_File);
    }

    IFACEMETHODIMP Save(LPCOLESTR pszFileName, BOOL fRemember) override
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP SaveCompleted(LPCOLESTR pszFileName) override
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCurFile(LPOLESTR *ppszFileName) override
    {
        return E_NOTIMPL;
    }

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CMscExtractIcon)

    BEGIN_COM_MAP(CMscExtractIcon)
        COM_INTERFACE_ENTRY_IID(IID_IExtractIconW, IExtractIconW)
        COM_INTERFACE_ENTRY_IID(IID_IPersistFile, IPersistFile)
    END_COM_MAP()
};

BEGIN_OBJECT_MAP(ObjectMap)
    OBJECT_ENTRY(CLSID_MscExtractIcon, CMscExtractIcon)
END_OBJECT_MAP()

static HRESULT GetAttributeValue(IXMLDOMNode *pnode, LPCWSTR name, BSTR *pout)
{
    CComPtr<IXMLDOMNamedNodeMap> pMap;
    HRESULT hr = E_OUTOFMEMORY;
    BSTR bsname = SysAllocString(name);
    *pout = NULL;
    if (bsname && SUCCEEDED(hr = pnode->get_attributes(&pMap)))
    {
        if (SUCCEEDED(hr = pMap->getNamedItem(bsname, &pnode)))
        {
            hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            if (pnode)
            {
                hr = pnode->get_text(pout);
                if (SUCCEEDED(hr) && !*pout)
                    hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                pnode->Release();
            }
        }
    }
    SysFreeString(bsname);
    return hr;
}

static HRESULT LoadXmlFromVariant(VARIANT *pVar, IXMLDOMDocument2 **ppDoc)
{
    VARIANT_BOOL succ = VARIANT_FALSE;
    HRESULT hr = CoCreateInstance(CLSID_DOMDocument30, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IXMLDOMDocument, (void**)ppDoc);
    if (FAILED(hr))
        return hr;
    else if (SUCCEEDED(hr = (*ppDoc)->load(*pVar, &succ)) && succ)
        return hr;
    (*ppDoc)->Release();
    return SUCCEEDED(hr) ? E_FAIL : hr;
}

HRESULT CMscExtractIcon::GetIconLocationFromMsc(PWSTR pszIconFile, UINT cchMax, int *piIndex)
{
    if (!m_File)
        return E_UNEXPECTED;

    CComPtr<IStream> pStream;
    HRESULT hr = SHCreateStreamOnFileW(m_File, STGM_READ | STGM_SHARE_DENY_WRITE, &pStream);
    if (FAILED(hr))
        return hr;
    VARIANT v;
    V_VT(&v) = VT_UNKNOWN;
    V_UNKNOWN(&v) = pStream;
    CComPtr<IXMLDOMDocument2> pDoc;
    if (FAILED(hr = LoadXmlFromVariant(&v, &pDoc)))
        return hr;

    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = const_cast<PWSTR>(L"XPath");
    pDoc->setProperty(const_cast<PWSTR>(L"SelectionLanguage"), v);
    CComPtr<IXMLDOMNode> pDomNode;
    hr = pDoc->selectSingleNode(const_cast<PWSTR>(L"/MMC_ConsoleFile/VisualAttributes/Icon"), &pDomNode);
    if (FAILED(hr))
        return hr;

    BSTR bstr;
    int index = 0;
    if (SUCCEEDED(GetAttributeValue(pDomNode, L"Index", &bstr)))
    {
        index = StrToIntW(bstr);
        SysFreeString(bstr);
    }
    *piIndex = index;

    if (SUCCEEDED(hr = GetAttributeValue(pDomNode, L"File", &bstr)))
    {
        hr = StringCchCopyW(pszIconFile, cchMax, bstr);
        SysFreeString(bstr);
    }
    return hr;
}

EXTERN_C BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
            g_hInstance = hInstance;
            DisableThreadLibraryCalls(hInstance);
            g_Module.Init(ObjectMap, hInstance, NULL);
            break;
    }

    return TRUE;
}

STDAPI DllCanUnloadNow()
{
    return g_Module.DllCanUnloadNow();
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    return g_Module.DllGetClassObject(rclsid, riid, ppv);
}

static HRESULT RegWriteString(PCWSTR pszKey, PCWSTR pszName, PCWSTR pszValue)
{
    DWORD cb = (lstrlenW(pszValue) + 1) * sizeof(*pszValue);
    DWORD res = SHSetValueW(HKEY_LOCAL_MACHINE, pszKey, pszName, REG_SZ, pszValue, cb);
    return res ? HRESULT_FROM_WIN32(res) : S_OK;
}

STDAPI DllRegisterServer()
{
    WCHAR buf[MAX_PATH], path[MAX_PATH];

    GetModuleFileNameW(g_hInstance, path, _countof(path));
    wsprintfW(buf, L"Software\\Classes\\%S", "CLSID\\" CLSIDSTR "\\InprocServer32");
    RegWriteString(buf, NULL, path);
    RegWriteString(buf, L"ThreadingModel", L"Apartment");

    wsprintfW(buf, L"Software\\Classes\\%S", "mscfile\\shellex\\IconHandler");
    wsprintfW(path, L"%S", CLSIDSTR);
    RegWriteString(buf, NULL, path);
    RegWriteString(L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", path, L"MMC Icon Handler");

    wsprintfW(buf, L"Software\\Classes\\%S", "mscfile\\DefaultIcon");
    return RegWriteString(buf, NULL, L"%1");
}

STDAPI DllUnregisterServer()
{
    WCHAR buf[MAX_PATH];

    wsprintfW(buf, L"Software\\Classes\\%S", "CLSID\\" CLSIDSTR);
    SHDeleteKeyW(HKEY_LOCAL_MACHINE, buf);

    wsprintfW(buf, L"%S", CLSIDSTR);
    SHDeleteValueW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", buf);

    wsprintfW(buf, L"Software\\Classes\\%S", "mscfile\\shellex\\IconHandler");
    SHDeleteKeyW(HKEY_LOCAL_MACHINE, buf);
    wsprintfW(buf, L"Software\\Classes\\%S", "mscfile\\shellex");
    SHDeleteEmptyKeyW(HKEY_LOCAL_MACHINE, buf);

    wsprintfW(buf, L"Software\\Classes\\%S", "mscfile\\DefaultIcon");
    return SHDeleteKeyW(HKEY_LOCAL_MACHINE, buf) ? E_FAIL : S_OK;
}
