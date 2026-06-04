recreation_add_module(asset
  src/vfs.cc
  src/asset_database.cc
)

target_link_libraries(recreation_asset PUBLIC recreation::core)
