
remove_definitions(-D_WIN32_WINNT=0x502)
add_definitions(-D_WIN32_WINNT=0x600)

spec2def(pdh.dll pdh.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    pdh_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/pdh_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/pdh.def)

add_library(pdh MODULE ${SOURCE})
set_module_type(pdh win32dll)
target_link_libraries(pdh wine)
add_importlibs(pdh msvcrt kernel32_vista kernel32 ntdll)
add_cd_file(TARGET pdh DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(pdh) # CORE-5743: No ARRAY_SIZE macro
