file(READ ${SPV} hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
get_filename_component(dir ${HEADER} DIRECTORY)
file(MAKE_DIRECTORY ${dir})
file(WRITE ${HEADER}
  "// generated from ${SPV}, do not edit\n"
  "static const unsigned char k_${SYMBOL}[] = {\n${bytes}\n};\n")
