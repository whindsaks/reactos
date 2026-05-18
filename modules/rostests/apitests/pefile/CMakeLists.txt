
add_executable(pefile_apitest ntoskrnl.c delayload.c testlist.c)
set_module_type(pefile_apitest win32cui)
add_importlibs(pefile_apitest msvcrt kernel32)
add_delay_importlibs(pefile_apitest gdi32 user32 uxtheme imm32 msctf comctl32 comdlg32 ole32 oleaut32 oleacc shell32 shlwapi shdocvw ieframe msi mspatcha winhttp wininet urlmon samlib samsrv lsasrv advpack setupapi secur32 cryptnet crypt32 cryptui wintrust userenv mlang mpr dhcpcsvc iphlpapi netapi32 dbghelp apphelp sfc_os msvfw32 usp10 odbccp32 msimg32 winmm windowscodecs version)
add_rostests_file(TARGET pefile_apitest)
