
add_definitions(-D__WINESRC__)
spec2def(mspatcha.dll mspatcha.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    mspatcha_main.c
    lzx.c
    lzx.h
    ${CMAKE_CURRENT_BINARY_DIR}/mspatcha_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/mspatcha.def)

add_library(mspatcha MODULE ${SOURCE} version.rc)
set_module_type(mspatcha win32dll)
target_link_libraries(mspatcha wine)
add_importlibs(mspatcha msvcrt kernel32 ntdll)
add_cd_file(TARGET mspatcha DESTINATION reactos/system32 FOR all)
