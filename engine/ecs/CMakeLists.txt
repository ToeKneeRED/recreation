recreation_add_module(ecs
  src/archetype.cc
  src/world.cc
  src/scheduler.cc
)

target_link_libraries(recreation_ecs PUBLIC recreation::core)
