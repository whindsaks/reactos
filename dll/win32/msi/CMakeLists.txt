
remove_definitions(-D_WIN32_WINNT=0x502 -D_CRT_NON_CONFORMING_SWPRINTFS)
add_definitions(-D_WIN32_WINNT=0x600)

add_definitions(-D__WINESRC__ -D__ROS_LONG64__ -DMSIRUNMODE=MSIRUNMODE_T)
spec2def(msi.dll msi.spec ADD_IMPORTLIB)
generate_idl_iids(msiserver.idl)
generate_idl_iids(winemsi.idl)

list(APPEND SOURCE
    action.c
    alter.c
    appsearch.c
    assembly.c
    automation.c
    classes.c
    create.c
    database.c
    delete.c
    dialog.c
    distinct.c
    drop.c
    files.c
    font.c
    format.c
    handle.c
    insert.c
    install.c
    media.c
    msi.c
    msi_main.c
    msiquery.c
    package.c
    patch.c
    record.c
    registry.c
    script.c
    select.c
    source.c
    storages.c
    streams.c
    string.c
    table.c
    tokenize.c
    update.c
    upgrade.c
    where.c)

BISON_TARGET(cond cond.y ${CMAKE_CURRENT_BINARY_DIR}/cond.tab.c)
BISON_TARGET(sql sql.y ${CMAKE_CURRENT_BINARY_DIR}/sql.tab.c)

set(OLD_IDL_FLAGS ${IDL_FLAGS})
set(IDL_FLAGS ${IDL_FLAGS} --prefix-server=s_)
add_rpc_files(client winemsi.idl)
add_rpc_files(server winemsi.idl)
set(IDL_FLAGS ${OLD_IDL_FLAGS})

list(APPEND PCH_SKIP_SOURCE
    custom.c    # Need custom defines for WIN32_NO_STATUS
    suminfo.c   # The only file without NONAMELESSUNION
    ${BISON_cond_OUTPUTS}
    ${BISON_sql_OUTPUTS}
    ${CMAKE_CURRENT_BINARY_DIR}/msiserver_i.c
    ${CMAKE_CURRENT_BINARY_DIR}/winemsi_c.c
    ${CMAKE_CURRENT_BINARY_DIR}/winemsi_s.c
    ${CMAKE_CURRENT_BINARY_DIR}/msi_stubs.c)

add_library(msi MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    msi.rc
    ${CMAKE_CURRENT_BINARY_DIR}/msi.def)

if(MSVC)
    # Disable warning C4090: 'function': different 'const' qualifiers
    # Disable warning C4133: 'function': incompatible types - from 'UINT *' to 'MSIINSTALLCONTEXT *'
    # Disable warning C4146: unary minus operator applied to unsigned type, result still unsigned
    # Disable warning C4312: 'type cast': conversion from 'unsigned int' to 'HANDLE' of greater size
    target_compile_options(msi PRIVATE /wd4090 /wd4133 /wd4146 /wd4312)
endif()

add_idl_headers(msi_idlheader msiserver.idl winemsi.idl)
add_typelib(msiserver.idl winemsi.idl)
set_source_files_properties(msi.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/msiserver.tlb;${CMAKE_CURRENT_BINARY_DIR}/winemsi.tlb")
set_module_type(msi win32dll)
target_link_libraries(msi uuid ${PSEH_LIB} wine_dll_register oldnames)
add_dependencies(msi msi_idlheader)
add_delay_importlibs(msi odbccp32 crypt32 wintrust apphelp)
add_importlibs(msi advapi32 advapi32_vista cabinet comctl32 gdi32 ole32 oleaut32 shell32 shlwapi rpcrt4 urlmon user32 version wininet sxs
    #FIXME : imagehlp, mspatcha should be in delayed imports
    imagehlp
    mspatcha
    msvcrt
    kernel32_vista
    kernel32
    ntdll)

add_pch(msi precomp.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET msi DESTINATION reactos/system32 FOR all)
