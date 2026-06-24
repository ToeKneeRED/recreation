# NVIDIA DLSS (NGX) upscaler SDK, downloaded via tools/get_dlss.sh. Ships
# headers and a prebuilt Linux static lib (libnvsdk_ngx.a). The actual DLSS
# inference snippet is a separate .so loaded at runtime; NGX is told where to
# find it through RECREATION_DLSS_LIB_DIR.
set(DLSS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/DLSS)

add_library(recreation_dlss STATIC IMPORTED GLOBAL)
set_target_properties(recreation_dlss PROPERTIES
  IMPORTED_LOCATION ${DLSS_DIR}/lib/Linux_x86_64/libnvsdk_ngx.a
  INTERFACE_INCLUDE_DIRECTORIES ${DLSS_DIR}/include
  INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS}"
  INTERFACE_COMPILE_DEFINITIONS "RECREATION_DLSS_LIB_DIR=\"${DLSS_DIR}/lib/Linux_x86_64/rel\"")
add_library(recreation::dlss ALIAS recreation_dlss)
