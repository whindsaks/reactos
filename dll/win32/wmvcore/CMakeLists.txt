
spec2def(wmvcore.dll wmvcore.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    reader.c
    wmvcore_main.c
    writer.c
    ${CMAKE_CURRENT_BINARY_DIR}/wmvcore_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/wmvcore.def)

add_library(wmvcore MODULE ${SOURCE} version.rc)
set_module_type(wmvcore win32dll)
target_link_libraries(wmvcore wine)
add_importlibs(wmvcore msvcrt kernel32 ntdll)
add_cd_file(TARGET wmvcore DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(wmvcore) # CORE-5743: No CONST_VTABLE
