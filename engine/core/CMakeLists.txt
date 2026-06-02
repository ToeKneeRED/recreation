recreation_add_module(core
  src/log.cc
  src/job_system.cc
  src/frame_timer.cc
  src/window.cc
)

target_link_libraries(recreation_core PUBLIC $<$<PLATFORM_ID:Linux>:pthread>)

if(ANDROID)
  target_link_libraries(recreation_core PUBLIC log android)
endif()
