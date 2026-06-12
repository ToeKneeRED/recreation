add_executable(recreation
  main.cc
  engine.cc
  fly_camera.cc
  debug_ui.cc
  game_ui.cc
)

target_link_libraries(recreation PRIVATE
  recreation::core
  recreation::ecs
  recreation::asset
  recreation::render
  recreation::physics
  recreation::bethesda
  recreation::world
  recreation::net
  recreation::script
)
recreation_set_warnings(recreation)

if(TARGET recreation_imgui)
  target_link_libraries(recreation PRIVATE recreation::imgui)
  target_compile_definitions(recreation PRIVATE RECREATION_HAS_IMGUI=1)
endif()

# libultragui HUD: only the windowed client builds it; the dedicated server
# compiles game_ui.cc to a stub. The draw list is rendered by gui_backend.cc
# with engine-embedded shaders.
if(RECREATION_HAS_UGUI)
  target_sources(recreation PRIVATE gui_backend.cc)
  target_link_libraries(recreation PRIVATE ultragui)
  target_compile_definitions(recreation PRIVATE RECREATION_HAS_UGUI=1)
  recreation_embed_shaders(recreation
    shaders/ugui_quad.vs.hlsl
    shaders/ugui_quad.ps.hlsl
    shaders/ugui_text.vs.hlsl
    shaders/ugui_text.ps.hlsl
  )
endif()

# Dedicated server: same engine, headless config baked in. Still links the
# renderer module since the engine owns one; it never initializes vulkan.
add_executable(recreation-server
  server_main.cc
  engine.cc
  fly_camera.cc
  debug_ui.cc
  game_ui.cc
)

target_link_libraries(recreation-server PRIVATE
  recreation::core
  recreation::ecs
  recreation::asset
  recreation::render
  recreation::physics
  recreation::bethesda
  recreation::world
  recreation::net
  recreation::script
)
recreation_set_warnings(recreation-server)
