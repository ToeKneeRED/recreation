#include "render/ambient_occlusion.h"

#include "core/log.h"
#include "render/raytracing.h"
#include "render/rhi/device.h"
#include "render/shader_util.h"
#include "shaders/rtao_blur_cs_hlsl.h"
#include "shaders/rtao_cs_hlsl.h"

namespace rec::render {
namespace {

constexpr VkFormat kAccumFormat = VK_FORMAT_R16G16_SFLOAT;
constexpr VkFormat kAoFormat = VK_FORMAT_R8_UNORM;

struct TracePush {
  Mat4 inv_view_proj;
  f32 inv_size[2];
  f32 radius;
  f32 frame_index;
  u32 ray_count;
  u32 reset_history;
  f32 pad[2];
};

struct BlurPush {
  f32 inv_size[2];
  f32 intensity;
  f32 pad;
};

}  // namespace

bool RtaoPass::Initialize(Device& device) {
  VkSamplerCreateInfo sampler_info{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(device.device(), &sampler_info, nullptr, &sampler_);

  auto make_pipeline = [&](const unsigned char* spv, size_t spv_size, u32 sampled_count,
                           bool with_tlas, u32 push_size, VkDescriptorSetLayout* set_layout,
                           VkPipelineLayout* layout, VkPipeline* pipeline) {
    VkDescriptorSetLayoutBinding bindings[6]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    u32 count = 1;
    for (u32 i = 0; i < sampled_count; ++i, ++count) {
      bindings[count].binding = count;
      bindings[count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      bindings[count].descriptorCount = 1;
      bindings[count].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    if (with_tlas) {
      bindings[count].binding = count;
      bindings[count].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
      bindings[count].descriptorCount = 1;
      bindings[count].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
      ++count;
    }

    VkDescriptorSetLayoutCreateInfo set_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = count;
    set_info.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device.device(), &set_info, nullptr, set_layout) !=
        VK_SUCCESS) {
      return false;
    }

    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size};
    VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = set_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device.device(), &layout_info, nullptr, layout) != VK_SUCCESS) {
      return false;
    }

    VkShaderModule module = CreateShaderModule(device.device(), spv, spv_size);
    if (module == VK_NULL_HANDLE) return false;
    VkComputePipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = "main";
    info.layout = *layout;
    VkResult result =
        vkCreateComputePipelines(device.device(), VK_NULL_HANDLE, 1, &info, nullptr, pipeline);
    vkDestroyShaderModule(device.device(), module, nullptr);
    return result == VK_SUCCESS;
  };

  if (!make_pipeline(k_rtao_cs_hlsl, sizeof(k_rtao_cs_hlsl), 4, true, sizeof(TracePush),
                     &trace_set_layout_, &trace_layout_, &trace_pipeline_) ||
      !make_pipeline(k_rtao_blur_cs_hlsl, sizeof(k_rtao_blur_cs_hlsl), 2, false,
                     sizeof(BlurPush), &blur_set_layout_, &blur_layout_, &blur_pipeline_)) {
    REC_ERROR("rtao pipeline creation failed");
    return false;
  }
  return true;
}

void RtaoPass::Resize(Device& device, VkExtent2D extent) {
  for (u32 i = 0; i < 2; ++i) {
    if (accum_[i].image) device.DestroyImage(accum_[i]);
    accum_[i] = device.CreateImage2D(kAccumFormat, extent,
                                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                                     VK_IMAGE_ASPECT_COLOR_BIT);
    accum_layouts_[i] = VK_IMAGE_LAYOUT_UNDEFINED;
  }
  extent_ = extent;
  history_valid_ = false;
}

void RtaoPass::Destroy(Device& device) {
  for (GpuImage& image : accum_) {
    if (image.image) device.DestroyImage(image);
  }
  if (trace_pipeline_) vkDestroyPipeline(device.device(), trace_pipeline_, nullptr);
  if (trace_layout_) vkDestroyPipelineLayout(device.device(), trace_layout_, nullptr);
  if (trace_set_layout_) vkDestroyDescriptorSetLayout(device.device(), trace_set_layout_, nullptr);
  if (blur_pipeline_) vkDestroyPipeline(device.device(), blur_pipeline_, nullptr);
  if (blur_layout_) vkDestroyPipelineLayout(device.device(), blur_layout_, nullptr);
  if (blur_set_layout_) vkDestroyDescriptorSetLayout(device.device(), blur_set_layout_, nullptr);
  if (sampler_) vkDestroySampler(device.device(), sampler_, nullptr);
  trace_pipeline_ = VK_NULL_HANDLE;
  trace_layout_ = VK_NULL_HANDLE;
  trace_set_layout_ = VK_NULL_HANDLE;
  blur_pipeline_ = VK_NULL_HANDLE;
  blur_layout_ = VK_NULL_HANDLE;
  blur_set_layout_ = VK_NULL_HANDLE;
  sampler_ = VK_NULL_HANDLE;
}

ResourceHandle RtaoPass::AddToGraph(RenderGraph& graph, RayTracingContext& raytracing,
                                    u32 tlas_slot, ResourceHandle depth, ResourceHandle normals,
                                    ResourceHandle motion, const Mat4& inv_view_proj,
                                    u32 frame_index) {
  u32 write_index = frame_index % 2;
  u32 read_index = 1 - write_index;
  ResourceHandle accum =
      graph.ImportImage("rtao_accum", accum_[write_index], &accum_layouts_[write_index]);
  ResourceHandle history =
      graph.ImportImage("rtao_history", accum_[read_index], &accum_layouts_[read_index]);
  ResourceHandle ao = graph.CreateTexture(
      {.name = "rtao", .format = kAoFormat, .width = extent_.width, .height = extent_.height});

  bool reset = !history_valid_;
  history_valid_ = true;

  graph.AddPass(
      "rtao_trace",
      [&](RenderGraph::PassBuilder& builder) {
        builder.Read(depth, ResourceUsage::kSampledCompute);
        builder.Read(normals, ResourceUsage::kSampledCompute);
        builder.Read(motion, ResourceUsage::kSampledCompute);
        builder.Read(history, ResourceUsage::kSampledCompute);
        builder.Write(accum, ResourceUsage::kStorageWrite);
      },
      [this, &raytracing, tlas_slot, depth, normals, motion, history, accum, inv_view_proj,
       frame_index, reset](PassContext& ctx) {
        VkDescriptorSet set = ctx.allocate_set(trace_set_layout_);

        VkDescriptorImageInfo images[5]{};
        images[0] = {.imageView = ctx.graph->image(accum).view,
                     .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        images[1] = {.sampler = sampler_, .imageView = ctx.graph->image(depth).view,
                     .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        images[2] = {.sampler = sampler_, .imageView = ctx.graph->image(normals).view,
                     .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        images[3] = {.sampler = sampler_, .imageView = ctx.graph->image(motion).view,
                     .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        images[4] = {.sampler = sampler_, .imageView = ctx.graph->image(history).view,
                     .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        VkAccelerationStructureKHR tlas = raytracing.tlas(tlas_slot);
        VkWriteDescriptorSetAccelerationStructureKHR tlas_info{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
        tlas_info.accelerationStructureCount = 1;
        tlas_info.pAccelerationStructures = &tlas;

        VkWriteDescriptorSet writes[6];
        for (u32 i = 0; i < 5; ++i) {
          writes[i] = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
          writes[i].dstSet = set;
          writes[i].dstBinding = i;
          writes[i].descriptorCount = 1;
          writes[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                            : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
          writes[i].pImageInfo = &images[i];
        }
        writes[5] = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[5].pNext = &tlas_info;
        writes[5].dstSet = set;
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        vkUpdateDescriptorSets(ctx.device->device(), 6, writes, 0, nullptr);

        TracePush push{};
        push.inv_view_proj = inv_view_proj;
        push.inv_size[0] = 1.0f / static_cast<f32>(extent_.width);
        push.inv_size[1] = 1.0f / static_cast<f32>(extent_.height);
        push.radius = settings_.radius;
        push.frame_index = static_cast<f32>(frame_index % 4096);
        push.ray_count = settings_.ray_count;
        push.reset_history = reset ? 1u : 0u;

        vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, trace_pipeline_);
        vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, trace_layout_, 0, 1,
                                &set, 0, nullptr);
        vkCmdPushConstants(ctx.cmd, trace_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push),
                           &push);
        vkCmdDispatch(ctx.cmd, (extent_.width + 7) / 8, (extent_.height + 7) / 8, 1);
      });

  graph.AddPass(
      "rtao_blur",
      [&](RenderGraph::PassBuilder& builder) {
        builder.Read(accum, ResourceUsage::kSampledCompute);
        builder.Read(depth, ResourceUsage::kSampledCompute);
        builder.Write(ao, ResourceUsage::kStorageWrite);
      },
      [this, accum, depth, ao](PassContext& ctx) {
        VkDescriptorSet set = ctx.allocate_set(blur_set_layout_);
        VkDescriptorImageInfo images[3]{};
        images[0] = {.imageView = ctx.graph->image(ao).view,
                     .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        images[1] = {.sampler = sampler_, .imageView = ctx.graph->image(accum).view,
                     .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        images[2] = {.sampler = sampler_, .imageView = ctx.graph->image(depth).view,
                     .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[3];
        for (u32 i = 0; i < 3; ++i) {
          writes[i] = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
          writes[i].dstSet = set;
          writes[i].dstBinding = i;
          writes[i].descriptorCount = 1;
          writes[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                            : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
          writes[i].pImageInfo = &images[i];
        }
        vkUpdateDescriptorSets(ctx.device->device(), 3, writes, 0, nullptr);

        BlurPush push{};
        push.inv_size[0] = 1.0f / static_cast<f32>(extent_.width);
        push.inv_size[1] = 1.0f / static_cast<f32>(extent_.height);
        push.intensity = settings_.intensity;

        vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, blur_pipeline_);
        vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, blur_layout_, 0, 1, &set,
                                0, nullptr);
        vkCmdPushConstants(ctx.cmd, blur_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push),
                           &push);
        vkCmdDispatch(ctx.cmd, (extent_.width + 7) / 8, (extent_.height + 7) / 8, 1);
      });
  return ao;
}

}  // namespace rec::render
