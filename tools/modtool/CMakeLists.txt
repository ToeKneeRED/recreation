add_executable(modtool main.cc)
target_link_libraries(modtool PRIVATE recreation::modstream)
recreation_set_warnings(modtool)
