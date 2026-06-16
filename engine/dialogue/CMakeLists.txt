recreation_add_module(dialogue
  dialogue.cc
)

# Reads DIAL/INFO records and their VMAD fragments through bethesda.
target_link_libraries(recreation_dialogue PUBLIC
  recreation::core
  recreation::bethesda)
