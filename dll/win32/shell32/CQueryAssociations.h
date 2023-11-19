#pragma once

class CQueryAssociations :
    public CComCoClass<CQueryAssociations, &CLSID_QueryAssociations>,
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IQueryAssociations
{
private:
    HKEY hkeySource;
    HKEY hkeyProgID;
    IQueryAssociations *m_pBase; // BaseClass
    ASSOCF m_InitFlags;
    WCHAR m_Init[MAX_PATH];
    WCHAR m_InitType;

    void Reset()
    {
        m_InitType = 0;
        m_Init[0] = UNICODE_NULL;
        if (hkeyProgID && hkeySource != hkeyProgID)
            RegCloseKey(hkeyProgID);
        if (hkeySource)
            RegCloseKey(hkeySource);
        hkeySource = hkeyProgID = NULL;
        IUnknown_Set((IUnknown**)&m_pBase, NULL);
    }

    inline SIZE_T HasKey() const { return SIZE_T(hkeySource) | SIZE_T(hkeyProgID); }
    inline LPWSTR GetInitString() { return m_Init; }
    LPCWSTR GetFileExt();

public:
    CQueryAssociations();
    ~CQueryAssociations();

    IQueryAssociations* GetBaseClass(ASSOCF flags);
    HRESULT GetValue(HKEY hkey, const WCHAR *name, void **data, DWORD *data_size = NULL);
    HRESULT GetCommand(const WCHAR *extra, WCHAR **command);
    HRESULT GetExecutable(LPCWSTR pszExtra, LPWSTR path, DWORD pathlen, DWORD *len);

    // *** IQueryAssociations methods ***
    virtual HRESULT STDMETHODCALLTYPE Init(ASSOCF flags, LPCWSTR pwszAssoc, HKEY hkProgid, HWND hwnd);
    virtual HRESULT STDMETHODCALLTYPE GetString(ASSOCF flags, ASSOCSTR str, LPCWSTR pwszExtra, LPWSTR pwszOut, DWORD *pcchOut);
    virtual HRESULT STDMETHODCALLTYPE GetKey(ASSOCF flags, ASSOCKEY key, LPCWSTR pwszExtra, HKEY *phkeyOut);
    virtual HRESULT STDMETHODCALLTYPE GetData(ASSOCF flags, ASSOCDATA data, LPCWSTR pwszExtra, void *pvOut, DWORD *pcbOut);
    virtual HRESULT STDMETHODCALLTYPE GetEnum(ASSOCF cfFlags, ASSOCENUM assocenum, LPCWSTR pszExtra, REFIID riid, LPVOID *ppvOut);

DECLARE_REGISTRY_RESOURCEID(IDR_QUERYASSOCIATIONS)
DECLARE_NOT_AGGREGATABLE(CQueryAssociations)
DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CQueryAssociations)
    COM_INTERFACE_ENTRY_IID(IID_IQueryAssociations, IQueryAssociations)
END_COM_MAP()
};
