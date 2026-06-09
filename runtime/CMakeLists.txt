add_executable(recreation
  src/main.cc
  src/engine.cc
)

target_include_directories(recreation PRIVATE src)
target_link_libraries(recreation PRIVATE
  recreation::core
  recreation::ecs
  recreation::asset
  recreation::render
  recreation::bethesda
  recreation::world
  recreation::net
  recreation::script
)
recreation_set_warnings(recreation)
