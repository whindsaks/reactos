
remove_definitions(-D_CRT_NON_CONFORMING_SWPRINTFS)
spec2def(riched20.dll riched20.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    caret.c
    clipboard.c
    context.c
    editor.c
    list.c
    paint.c
    para.c
    reader.c
    richole.c
    row.c
    run.c
    string.c
    style.c
    table.c
    txthost.c
    txtsrv.c
    undo.c
    wrap.c
    writer.c
    precomp.h)

list(APPEND riched20_rc_deps
    ${CMAKE_CURRENT_SOURCE_DIR}/riched_tom.rgs
    ${CMAKE_CURRENT_BINARY_DIR}/riched_tom.tlb)
set_source_files_properties(version.rc PROPERTIES OBJECT_DEPENDS "${riched20_rc_deps}")

add_library(riched20 MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/riched20.def)

if(MSVC AND ARCH STREQUAL "i386")
    # MSVC doesn't support __thiscall in C code
    target_compile_definitions(riched20 PRIVATE __ASM_USE_THISCALL_WRAPPER)
endif()

add_typelib(riched_tom.idl)
add_dependencies(riched20 stdole2)
set_module_type(riched20 win32dll)
target_link_libraries(riched20 wine uuid oldnames)
add_importlibs(riched20 ole32 oleaut32 usp10 imm32 user32 gdi32 msvcrt kernel32 ntdll)
add_pch(riched20 precomp.h SOURCE)
add_cd_file(TARGET riched20 DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(riched20) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
