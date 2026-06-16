recreation_add_module(net
  quest_replication.cc
  replication.cc
  session.cc
)

# zetanet carries the packets, nanobuf defines what is in them, world/asset
# provide the components the replication layer mirrors, quest supplies the
# journal state the sessions replicate.
target_link_libraries(recreation_net PUBLIC
  recreation::core
  recreation::ecs
  recreation::world
  recreation::quest
  recreation::nanobuf
  zetanet
)
