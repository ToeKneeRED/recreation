#include "render/post.h"

#include <base/containers/vector.h>

#include "core/log.h"
#include "render/shader_util.h"
#include "shaders/fullscreen_vs_hlsl.h"
#include "shaders/tonemap_ps_hlsl.h"

namespace rec::render {

std::unique_ptr<PostPass> PostPass::Create(Device& device, VkFormat output_format) {
  auto pass = std::unique_ptr<PostPass>(new PostPass(device));

  VkSamplerCreateInfo sampler_info{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(device.device(), &sampler_info, nullptr, &pass->sampler_);

  VkDescriptorSetLayoutBinding bindings[4]{};
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings[1] = bindings[0];
  bindings[1].binding = 1;
  bindings[2].binding = 2;
  bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[2].descriptorCount = 1;
  bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings[3] = bindings[0];
  bindings[3].binding = 3;  // grading strip lut

  VkDescriptorSetLayoutCreateInfo set_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set_info.bindingCount = 4;
  set_info.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(device.device(), &set_info, nullptr, &pass->set_layout_) !=
      VK_SUCCESS) {
    return nullptr;
  }

  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  push_range.size = sizeof(Params);

  VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &pass->set_layout_;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_range;
  if (vkCreatePipelineLayout(device.device(), &layout_info, nullptr, &pass->layout_) !=
      VK_SUCCESS) {
    return nullptr;
  }

  VkShaderModule vert =
      CreateShaderModule(device.device(), k_fullscreen_vs_hlsl, sizeof(k_fullscreen_vs_hlsl));
  VkShaderModule frag =
      CreateShaderModule(device.device(), k_tonemap_ps_hlsl, sizeof(k_tonemap_ps_hlsl));
  if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
    REC_ERROR("post shader module creation failed");
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

  VkPipelineVertexInputStateCreateInfo vertex_input{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

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
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

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
  rendering.pColorAttachmentFormats = &output_format;

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
  info.layout = pass->layout_;

  VkResult result =
      vkCreateGraphicsPipelines(device.device(), VK_NULL_HANDLE, 1, &info, nullptr,
                                &pass->pipeline_);
  vkDestroyShaderModule(device.device(), vert, nullptr);
  vkDestroyShaderModule(device.device(), frag, nullptr);
  if (result != VK_SUCCESS) {
    REC_ERROR("post pipeline creation failed");
    return nullptr;
  }
  if (!pass->CreateLut()) return nullptr;
  return pass;
}

namespace {

f32 Clamp01(f32 x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Applies a built-in grade to a neutral display-space color (tonemapped,
// pre-srgb). kNeutral is identity. Authored to read clearly: warm/cool shift
// white balance, cinematic does a teal-shadow / orange-highlight split with a
// touch of contrast.
void ApplyGrade(ColorGrade grade, f32 in[3], f32 out[3]) {
  f32 r = in[0], g = in[1], b = in[2];
  switch (grade) {
    case ColorGrade::kNeutral:
      break;
    case ColorGrade::kWarm:
      r *= 1.10f;
      g *= 1.02f;
      b *= 0.88f;
      break;
    case ColorGrade::kCool:
      r *= 0.88f;
      g *= 1.0f;
      b *= 1.12f;
      break;
    case ColorGrade::kCinematic: {
      f32 luma = 0.299f * r + 0.587f * g + 0.114f * b;
      // Teal in shadows, orange in highlights, lerped by luma.
      r += (1.0f - luma) * -0.04f + luma * 0.10f;
      g += (1.0f - luma) * 0.03f + luma * 0.04f;
      b += (1.0f - luma) * 0.10f + luma * -0.07f;
      r = (r - 0.5f) * 1.12f + 0.5f;  // gentle contrast
      g = (g - 0.5f) * 1.12f + 0.5f;
      b = (b - 0.5f) * 1.12f + 0.5f;
      break;
    }
  }
  out[0] = Clamp01(r);
  out[1] = Clamp01(g);
  out[2] = Clamp01(b);
}

}  // namespace

bool PostPass::CreateLut() {
  const u32 width = kLutSize * kLutSize;
  lut_ = device_.CreateImage2D(
      VK_FORMAT_R8G8B8A8_UNORM, {width, kLutSize},
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
  if (!lut_.image) return false;
  UploadLut(ColorGrade::kNeutral);
  return true;
}

void PostPass::UploadLut(ColorGrade grade) {
  const u32 size = kLutSize;
  const u32 width = size * size;
  base::Vector<u8> pixels(static_cast<size_t>(width) * size * 4);
  for (u32 b = 0; b < size; ++b) {
    for (u32 g = 0; g < size; ++g) {
      for (u32 r = 0; r < size; ++r) {
        f32 in[3] = {static_cast<f32>(r) / (size - 1), static_cast<f32>(g) / (size - 1),
                     static_cast<f32>(b) / (size - 1)};
        f32 out[3];
        ApplyGrade(grade, in, out);
        u8* p = &pixels[(static_cast<size_t>(g) * width + (b * size + r)) * 4];
        p[0] = static_cast<u8>(out[0] * 255.0f + 0.5f);
        p[1] = static_cast<u8>(out[1] * 255.0f + 0.5f);
        p[2] = static_cast<u8>(out[2] * 255.0f + 0.5f);
        p[3] = 255;
      }
    }
  }

  GpuBuffer staging = device_.CreateBufferWithData(
      {pixels.data(), pixels.size()}, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  bool first = !lut_ready_;
  device_.ImmediateSubmit([&](VkCommandBuffer cmd) {
    VkImageMemoryBarrier2 to_dst{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    to_dst.srcStageMask = first ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    to_dst.srcAccessMask = first ? 0 : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    to_dst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    to_dst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_dst.oldLayout = first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.image = lut_.image;
    to_dst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &to_dst;
    vkCmdPipelineBarrier2(cmd, &dep);

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width, size, 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, lut_.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    to_dst.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    to_dst.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_dst.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    to_dst.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    to_dst.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier2(cmd, &dep);
  });
  device_.DestroyBuffer(staging);
  lut_ready_ = true;
  lut_grade_ = grade;
}

void PostPass::SetGrade(ColorGrade grade) {
  if (grade == lut_grade_) return;
  device_.WaitIdle();  // the lut is shared across frames; drain before reupload
  UploadLut(grade);
}

PostPass::~PostPass() {
  if (pipeline_) vkDestroyPipeline(device_.device(), pipeline_, nullptr);
  if (layout_) vkDestroyPipelineLayout(device_.device(), layout_, nullptr);
  if (set_layout_) vkDestroyDescriptorSetLayout(device_.device(), set_layout_, nullptr);
  if (sampler_) vkDestroySampler(device_.device(), sampler_, nullptr);
  device_.DestroyImage(lut_);
}

void PostPass::Record(PassContext& ctx, VkImageView input, VkImageView bloom, VkBuffer exposure,
                      u64 exposure_size, VkImageView output, VkExtent2D output_extent,
                      const Params& params) {
  VkDescriptorSet set = ctx.allocate_set(set_layout_);
  VkDescriptorImageInfo images[3]{};
  images[0] = {sampler_, input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  images[1] = {sampler_, bloom, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  images[2] = {sampler_, lut_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorBufferInfo exposure_info{exposure, 0, exposure_size};
  VkWriteDescriptorSet writes[4];
  for (u32 i = 0; i < 2; ++i) {
    writes[i] = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[i].dstSet = set;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[i].pImageInfo = &images[i];
  }
  writes[2] = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  writes[2].dstSet = set;
  writes[2].dstBinding = 2;
  writes[2].descriptorCount = 1;
  writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[2].pBufferInfo = &exposure_info;
  writes[3] = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  writes[3].dstSet = set;
  writes[3].dstBinding = 3;
  writes[3].descriptorCount = 1;
  writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[3].pImageInfo = &images[2];
  vkUpdateDescriptorSets(device_.device(), 4, writes, 0, nullptr);

  VkRenderingAttachmentInfo color{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = output;
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // fully overwritten
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

  VkRenderingInfo rendering{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea = {{0, 0}, output_extent};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color;

  vkCmdBeginRendering(ctx.cmd, &rendering);
  VkViewport viewport{0, 0, static_cast<f32>(output_extent.width),
                      static_cast<f32>(output_extent.height), 0.0f, 1.0f};
  VkRect2D scissor{{0, 0}, output_extent};
  vkCmdSetViewport(ctx.cmd, 0, 1, &viewport);
  vkCmdSetScissor(ctx.cmd, 0, 1, &scissor);
  vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
  vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0,
                          nullptr);
  vkCmdPushConstants(ctx.cmd, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(params), &params);
  vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
  vkCmdEndRendering(ctx.cmd);
}

}  // namespace rec::render
