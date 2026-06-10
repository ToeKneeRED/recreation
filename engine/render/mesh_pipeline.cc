#include "render/mesh_pipeline.h"

#include <cstring>
#include <vector>

#include "asset/mesh.h"
#include "core/log.h"
#include "shaders/mesh_frag.h"
#include "shaders/mesh_vert.h"

namespace rec::render {
namespace {

VkShaderModule MakeModule(VkDevice device, const unsigned char* code, size_t size) {
  // The embedded arrays are byte aligned, spirv wants words.
  std::vector<u32> words((size + 3) / 4);
  std::memcpy(words.data(), code, size);

  VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = size;
  info.pCode = words.data();
  VkShaderModule module = VK_NULL_HANDLE;
  vkCreateShaderModule(device, &info, nullptr, &module);
  return module;
}

}  // namespace

std::unique_ptr<MeshPipeline> MeshPipeline::Create(Device& device, VkFormat color_format,
                                                   VkFormat depth_format) {
  auto pipeline = std::unique_ptr<MeshPipeline>(new MeshPipeline(device));

  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_range.size = sizeof(MeshPushConstants);

  VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_range;
  if (vkCreatePipelineLayout(device.device(), &layout_info, nullptr, &pipeline->layout_) !=
      VK_SUCCESS) {
    return nullptr;
  }

  VkShaderModule vert = MakeModule(device.device(), k_mesh_vert, sizeof(k_mesh_vert));
  VkShaderModule frag = MakeModule(device.device(), k_mesh_frag, sizeof(k_mesh_frag));
  if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
    REC_ERROR("mesh shader module creation failed");
    return nullptr;
  }

  VkPipelineShaderStageCreateInfo stages[2];
  stages[0] = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1] = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.stride = sizeof(asset::Vertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attributes[2]{};
  attributes[0] = {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
                   .offset = offsetof(asset::Vertex, position)};
  attributes[1] = {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
                   .offset = offsetof(asset::Vertex, normal)};

  VkPipelineVertexInputStateCreateInfo vertex_input{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = 2;
  vertex_input.pVertexAttributeDescriptions = attributes;

  VkPipelineInputAssemblyStateCreateInfo input_assembly{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  // TODO: back face culling once converted content settles winding order.
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth.depthTestEnable = VK_TRUE;
  depth.depthWriteEnable = VK_TRUE;
  depth.depthCompareOp = VK_COMPARE_OP_GREATER;  // reversed z

  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamic_states;

  VkPipelineRenderingCreateInfo rendering{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &color_format;
  rendering.depthAttachmentFormat = depth_format;

  VkGraphicsPipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.pNext = &rendering;
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vertex_input;
  info.pInputAssemblyState = &input_assembly;
  info.pViewportState = &viewport;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pDepthStencilState = &depth;
  info.pColorBlendState = &blend;
  info.pDynamicState = &dynamic;
  info.layout = pipeline->layout_;

  VkResult result = vkCreateGraphicsPipelines(device.device(), VK_NULL_HANDLE, 1, &info, nullptr,
                                              &pipeline->pipeline_);
  vkDestroyShaderModule(device.device(), vert, nullptr);
  vkDestroyShaderModule(device.device(), frag, nullptr);
  if (result != VK_SUCCESS) {
    REC_ERROR("mesh pipeline creation failed");
    return nullptr;
  }
  return pipeline;
}

MeshPipeline::~MeshPipeline() {
  if (pipeline_) vkDestroyPipeline(device_.device(), pipeline_, nullptr);
  if (layout_) vkDestroyPipelineLayout(device_.device(), layout_, nullptr);
}

void MeshPipeline::Bind(VkCommandBuffer cmd) {
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
}

void MeshPipeline::Draw(VkCommandBuffer cmd, const GpuMesh& mesh, const MeshPushConstants& push) {
  vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, &offset);
  vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
}

}  // namespace rec::render
