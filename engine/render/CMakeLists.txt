recreation_add_module(render
  renderer.cc
  render_graph.cc
  taa.cc
  raytracing.cc
  upscaler.cc
  vulkan/vk_device.cc
  vulkan/vk_swapchain.cc
  vulkan/vk_resources.cc
  vulkan/vma_impl.cc
)

target_link_libraries(recreation_render
  PUBLIC recreation::core recreation::asset volk GPUOpen::VulkanMemoryAllocator)

if(NOT MSVC)
  set_source_files_properties(vulkan/vma_impl.cc PROPERTIES COMPILE_OPTIONS -w)
endif()
