add_executable(esminfo main.cc)
target_link_libraries(esminfo PRIVATE recreation::bethesda)
recreation_set_warnings(esminfo)
