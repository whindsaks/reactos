add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/compmgmt.msc DESTINATION reactos/system32 FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/devmgmt.msc DESTINATION reactos/system32 FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/eventvwr.msc DESTINATION reactos/system32 FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/services.msc DESTINATION reactos/system32 FOR all)

add_subdirectory(devmgmt)
add_subdirectory(eventvwr)
add_subdirectory(servman)