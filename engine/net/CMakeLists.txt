recreation_add_module(net
  replication.cc
  session.cc
)

# zetanet carries the packets, nanobuf defines what is in them, world/asset
# provide the components the replication layer mirrors.
target_link_libraries(recreation_net PUBLIC
  recreation::core
  recreation::ecs
  recreation::world
  recreation::nanobuf
  zetanet
)
