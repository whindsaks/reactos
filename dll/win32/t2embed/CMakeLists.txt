
add_definitions(-D__WINESRC__)
spec2def(t2embed.dll t2embed.spec)

list(APPEND SOURCE
    main.c
    stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/t2embed_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/t2embed.def)

add_library(t2embed MODULE ${SOURCE} t2embed.rc)
set_module_type(t2embed win32dll UNICODE)
target_link_libraries(t2embed wine)
add_importlibs(t2embed gdi32 user32 advapi32 version msvcrt kernel32 ntdll)
add_cd_file(TARGET t2embed DESTINATION reactos/system32 FOR all)
