#include "bethesda/converters.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "bethesda/nif.h"
#include "bethesda/starfield_mesh.h"
#include "core/log.h"

namespace rec::bethesda {
namespace {

constexpr u32 kDdsMagic = FourCc('D', 'D', 'S', ' ');
constexpr u32 kDdpfFourCc = 0x4;
constexpr u32 kDdpfRgb = 0x40;
constexpr u32 kCaps2Cubemap = 0x200;

bool BlockCompressed(asset::TextureFormat format) {
  return format != asset::TextureFormat::kRgba8;
}

size_t MipChainSize(asset::TextureFormat format, u32 width, u32 height, u32 mips) {
  size_t total = 0;
  for (u32 m = 0; m < mips; ++m) {
    u32 w = std::max(1u, width >> m);
    u32 h = std::max(1u, height >> m);
    if (BlockCompressed(format)) {
      size_t block = (format == asset::TextureFormat::kBc1 ||
                      format == asset::TextureFormat::kBc4)
                         ? 8
                         : 16;
      total += ((w + 3) / 4) * ((h + 3) / 4) * block;
    } else {
      total += static_cast<size_t>(w) * h * 4;
    }
  }
  return total;
}

// Normal maps and other data textures must stay linear. Vanilla assets
// follow the _n / _msn suffix convention.
bool PathIsLinearData(std::string_view path) {
  size_t dot = path.rfind('.');
  std::string_view stem = dot == std::string_view::npos ? path : path.substr(0, dot);
  return stem.ends_with("_n") || stem.ends_with("_msn") || stem.ends_with("_normal");
}

}  // namespace

base::UniquePointer<asset::Texture> ConvertDds(ByteSpan data, asset::AssetId id,
                                               std::string_view path) {
  if (data.size() < 128) return nullptr;
  u32 magic;
  std::memcpy(&magic, data.data(), 4);
  if (magic != kDdsMagic) return nullptr;

  auto texture = base::MakeUnique<asset::Texture>();
  texture->id = id;
  std::memcpy(&texture->height, data.data() + 12, 4);
  std::memcpy(&texture->width, data.data() + 16, 4);
  std::memcpy(&texture->mip_count, data.data() + 28, 4);
  if (texture->mip_count == 0) texture->mip_count = 1;

  u32 pf_flags, fourcc, caps2;
  std::memcpy(&pf_flags, data.data() + 80, 4);
  std::memcpy(&fourcc, data.data() + 84, 4);
  std::memcpy(&caps2, data.data() + 112, 4);
  if (caps2 & kCaps2Cubemap) return nullptr;  // environment maps, unused

  size_t data_offset = 128;
  bool srgb_from_format = false;
  if ((pf_flags & kDdpfFourCc) && fourcc == FourCc('D', 'X', '1', '0')) {
    if (data.size() < 148) return nullptr;
    u32 dxgi;
    std::memcpy(&dxgi, data.data() + 128, 4);
    data_offset = 148;
    // Bethesda authors color maps (albedo, glow) as the plain UNORM DXGI
    // formats, not the _SRGB variants, and relies on the texture's usage to
    // know they are sRGB. So a UNORM color-capable format is ambiguous: it may
    // be an sRGB albedo or a linear normal/data map stored in the same block
    // format. Only the explicit _SRGB variants are unambiguously sRGB; the
    // rest defer to the path heuristic below (matching the FourCC path).
    switch (dxgi) {
      case 71: texture->format = asset::TextureFormat::kBc1; break;
      case 72: texture->format = asset::TextureFormat::kBc1; srgb_from_format = true; break;
      case 74: texture->format = asset::TextureFormat::kBc2; break;
      case 75: texture->format = asset::TextureFormat::kBc2; srgb_from_format = true; break;
      case 77: texture->format = asset::TextureFormat::kBc3; break;
      case 78: texture->format = asset::TextureFormat::kBc3; srgb_from_format = true; break;
      case 80: texture->format = asset::TextureFormat::kBc4; break;
      case 83: texture->format = asset::TextureFormat::kBc5; break;
      case 87:  // B8G8R8A8_UNORM: swizzled to RGBA below
      case 28: texture->format = asset::TextureFormat::kRgba8; break;
      case 91:  // B8G8R8A8_UNORM_SRGB
      case 29: texture->format = asset::TextureFormat::kRgba8; srgb_from_format = true; break;
      case 98: texture->format = asset::TextureFormat::kBc7; break;
      case 99: texture->format = asset::TextureFormat::kBc7; srgb_from_format = true; break;
      default:
        REC_WARN("unsupported dxgi format {} in {}", dxgi, path);
        return nullptr;
    }
    // B8G8R8A8 ships blue and red swapped relative to the RGBA8 Vulkan format.
    if (dxgi == 87 || dxgi == 91) {
      texture->data.assign(data.begin() + static_cast<std::ptrdiff_t>(data_offset), data.end());
      for (size_t i = 0; i + 3 < texture->data.size(); i += 4) {
        std::swap(texture->data[i], texture->data[i + 2]);
      }
    }
  } else if (pf_flags & kDdpfFourCc) {
    if (fourcc == FourCc('D', 'X', 'T', '1')) texture->format = asset::TextureFormat::kBc1;
    else if (fourcc == FourCc('D', 'X', 'T', '3')) texture->format = asset::TextureFormat::kBc3;
    else if (fourcc == FourCc('D', 'X', 'T', '5')) texture->format = asset::TextureFormat::kBc3;
    else if (fourcc == FourCc('A', 'T', 'I', '1') || fourcc == FourCc('B', 'C', '4', 'U'))
      texture->format = asset::TextureFormat::kBc4;
    else if (fourcc == FourCc('A', 'T', 'I', '2') || fourcc == FourCc('B', 'C', '5', 'U'))
      texture->format = asset::TextureFormat::kBc5;
    else {
      REC_WARN("unsupported dds fourcc in {}", path);
      return nullptr;
    }
  } else if (pf_flags & kDdpfRgb) {
    u32 bit_count, r_mask;
    std::memcpy(&bit_count, data.data() + 88, 4);
    std::memcpy(&r_mask, data.data() + 92, 4);
    if (bit_count != 32) {
      REC_WARN("unsupported uncompressed dds ({} bpp) in {}", bit_count, path);
      return nullptr;
    }
    texture->format = asset::TextureFormat::kRgba8;
    texture->data.assign(data.begin() + static_cast<std::ptrdiff_t>(data_offset), data.end());
    if (r_mask == 0x00ff0000) {  // BGRA on disk
      for (size_t i = 0; i + 3 < texture->data.size(); i += 4) {
        std::swap(texture->data[i], texture->data[i + 2]);
      }
    }
  } else {
    REC_WARN("unsupported dds pixel format in {}", path);
    return nullptr;
  }

  if (texture->data.empty()) {
    texture->data.assign(data.begin() + static_cast<std::ptrdiff_t>(data_offset), data.end());
  }

  // Trim the mip chain to what is actually present.
  while (texture->mip_count > 1 &&
         MipChainSize(texture->format, texture->width, texture->height, texture->mip_count) >
             texture->data.size()) {
    --texture->mip_count;
  }
  if (MipChainSize(texture->format, texture->width, texture->height, 1) > texture->data.size()) {
    REC_WARN("truncated dds: {}", path);
    return nullptr;
  }

  bool color = texture->format == asset::TextureFormat::kBc1 ||
               texture->format == asset::TextureFormat::kBc2 ||
               texture->format == asset::TextureFormat::kBc3 ||
               texture->format == asset::TextureFormat::kBc7 ||
               texture->format == asset::TextureFormat::kRgba8;
  if (srgb_from_format) texture->is_srgb = true;
  else texture->is_srgb = color && !PathIsLinearData(path);
  if (!color) texture->is_srgb = false;
  return texture;
}

namespace {

std::string NormalizeTexturePathLocal(std::string_view raw) {
  if (raw.empty()) return {};
  std::string path = asset::NormalizePath(raw);
  size_t anchor = path.rfind("textures/");
  if (anchor != std::string::npos) return path.substr(anchor);
  return "textures/" + path;
}

// One length-prefixed, null-terminated string from a BGSM/BGEM at *cursor.
std::string ReadBgsmString(ByteSpan data, size_t* cursor) {
  if (*cursor + 4 > data.size()) return {};
  u32 length;
  std::memcpy(&length, data.data() + *cursor, 4);
  *cursor += 4;
  if (length > 1024 || *cursor + length > data.size()) return {};
  std::string s(reinterpret_cast<const char*>(data.data() + *cursor), length);
  *cursor += length;
  while (!s.empty() && s.back() == '\0') s.pop_back();
  return s;
}

// Fallout 4 BGSM (lighting) / BGEM (effect) material file, version 2. The
// pre-texture header is a fixed size, so the texture string block starts at a
// constant offset. Slot 0 is the base/diffuse texture; for BGSM slot 1 is the
// normal map (BGEM slot 1 is a greyscale mask, not a normal, so it is ignored).
constexpr size_t kBgsmV2TextureOffset = 63;

// BGSM lays ten texture slots, then a fixed block (flags, a scalar, the
// specular colour and strength), then the smoothness float. Verified against
// shipped FO4 materials: the smoothness sits this far past the slots and reads
// in [0,1] for ~99% of them (the rest are clamped).
constexpr size_t kBgsmV2TextureSlots = 10;
constexpr size_t kBgsmV2SmoothnessGap = 28;

base::UniquePointer<asset::Material> ConvertBgsm(ByteSpan data, asset::AssetId id,
                                                 std::string_view) {
  auto material = base::MakeUnique<asset::Material>();
  material->id = id;
  BgsmMaterial parsed;
  if (ParseBgsm(data, &parsed)) {
    if (!parsed.diffuse.empty()) material->base_color = asset::MakeAssetId(parsed.diffuse);
    if (!parsed.normal.empty()) material->normal = asset::MakeAssetId(parsed.normal);
    if (parsed.roughness >= 0.0f) material->roughness_factor = parsed.roughness;
  }
  return material;
}

}  // namespace

// roughness stays negative (unknown) for BGEM or a short read, so the caller
// keeps its own default; for BGSM it is 1 - smoothness.
bool ParseBgsm(ByteSpan data, BgsmMaterial* out) {
  *out = BgsmMaterial{};
  if (data.size() < kBgsmV2TextureOffset + 8) return false;
  const bool bgsm = std::memcmp(data.data(), "BGSM", 4) == 0;
  const bool bgem = std::memcmp(data.data(), "BGEM", 4) == 0;
  if (!bgsm && !bgem) return false;
  u32 version;
  std::memcpy(&version, data.data() + 4, 4);
  if (version != 2) return false;  // FO76 (v20+) has a different header

  size_t cursor = kBgsmV2TextureOffset;
  std::string base = ReadBgsmString(data, &cursor);
  std::string second = ReadBgsmString(data, &cursor);
  out->diffuse = NormalizeTexturePathLocal(base);
  out->normal = bgsm ? NormalizeTexturePathLocal(second) : std::string();

  if (bgsm) {
    for (size_t i = 2; i < kBgsmV2TextureSlots; ++i) ReadBgsmString(data, &cursor);
    cursor += kBgsmV2SmoothnessGap;
    if (cursor + 4 <= data.size()) {
      f32 smoothness;
      std::memcpy(&smoothness, data.data() + cursor, 4);
      out->roughness = 1.0f - std::clamp(smoothness, 0.0f, 1.0f);
    }
  }
  return !out->diffuse.empty();
}

namespace {

// Starfield NIFs reference external ".mesh" geometry by hash rather than
// inlining vertices, so its converter loads each referenced mesh from the vfs,
// bakes the node transform into the vertices, and assigns one default material
// (textures are out of scope for now). Returns null when nothing converts.
base::UniquePointer<asset::Mesh> ConvertStarfieldNif(asset::AssetDatabase& database, ByteSpan data,
                                                     asset::AssetId id, std::string_view path) {
  base::Vector<StarfieldGeometryRef> refs;
  if (!ParseStarfieldNif(data, &refs)) return nullptr;

  auto mesh = base::MakeUnique<asset::Mesh>();
  mesh->id = id;
  mesh->lods.emplace_back();
  asset::MeshLod& lod = mesh->lods[0];

  // One shared default material: mid gray, mostly rough. base_color stays unset
  // (no texture), so the renderer falls back to base_color_factor.
  asset::Material material;
  material.id = asset::MakeAssetId(std::string(path) + "#m0");
  for (int k = 0; k < 3; ++k) material.base_color_factor[k] = 0.5f;
  material.roughness_factor = 0.8f;
  material.metallic_factor = 0;
  database.AddMaterial(material);

  f32 bounds_min[3] = {1e30f, 1e30f, 1e30f};
  f32 bounds_max[3] = {-1e30f, -1e30f, -1e30f};
  for (const StarfieldGeometryRef& ref : refs) {
    auto bytes = database.vfs().Read(ref.mesh_path);
    if (!bytes) continue;
    StarfieldMeshData geometry;
    if (!ParseStarfieldMesh(ByteSpan(bytes->data(), bytes->size()), &geometry)) continue;

    u32 vertex_base = static_cast<u32>(lod.vertices.size());
    u32 index_offset = static_cast<u32>(lod.indices.size());
    for (const asset::Vertex& src : geometry.vertices) {
      asset::Vertex v = src;
      // Bake the node-chain transform: p' = rotation * p * scale + translation.
      for (int i = 0; i < 3; ++i) {
        v.position[i] = (ref.rotation[i * 3] * src.position[0] +
                         ref.rotation[i * 3 + 1] * src.position[1] +
                         ref.rotation[i * 3 + 2] * src.position[2]) *
                            ref.scale +
                        ref.translation[i];
        v.normal[i] = ref.rotation[i * 3] * src.normal[0] +
                      ref.rotation[i * 3 + 1] * src.normal[1] +
                      ref.rotation[i * 3 + 2] * src.normal[2];
      }
      for (int k = 0; k < 3; ++k) {
        bounds_min[k] = std::min(bounds_min[k], v.position[k]);
        bounds_max[k] = std::max(bounds_max[k], v.position[k]);
      }
      lod.vertices.push_back(v);
    }
    for (u32 index : geometry.indices) lod.indices.push_back(vertex_base + index);

    asset::Submesh submesh;
    submesh.index_offset = index_offset;
    submesh.index_count = static_cast<u32>(geometry.indices.size());
    submesh.material = material.id;
    lod.submeshes.push_back(submesh);
  }

  if (lod.vertices.empty()) return nullptr;
  for (int k = 0; k < 3; ++k) mesh->bounds_center[k] = (bounds_min[k] + bounds_max[k]) * 0.5f;
  f32 radius_sq = 0;
  for (int k = 0; k < 3; ++k) {
    f32 d = bounds_max[k] - mesh->bounds_center[k];
    radius_sq += d * d;
  }
  mesh->bounds_radius = std::sqrt(radius_sq);
  return mesh;
}

}  // namespace

void RegisterConverters(asset::AssetDatabase& database, const GameProfile& profile) {
  if (profile.game == Game::kStarfield) {
    database.RegisterMeshConverter(
        ".nif", [&database](ByteSpan data, asset::AssetId id, std::string_view path) {
          return ConvertStarfieldNif(database, data, id, path);
        });
    database.RegisterTextureConverter(".dds", ConvertDds);
    REC_INFO("registered bethesda converters for {}", profile.name);
    return;
  }
  // The NIF converter synthesizes materials from the shader property blocks
  // and pre-loads their textures, so a converted mesh's submesh material ids
  // always resolve against the database.
  database.RegisterMeshConverter(
      ".nif", [&database](ByteSpan data, asset::AssetId id, std::string_view path) {
        NifConversion conversion = ConvertNifScene(data, id, path);
        if (!conversion.mesh) return base::UniquePointer<asset::Mesh>();
        for (const std::string& texture : conversion.texture_paths) {
          database.LoadTexture(texture);
        }
        // Fallout 4 binds textures through a .bgsm/.bgem material file; the
        // material file is authoritative, so it overrides the inline texture
        // set the lighting shader may also carry (FO4 NIFs ship both, and the
        // inline slots are unreliable -- e.g. architecture meshes bind a normal
        // map there, which would render as a purple albedo).
        for (size_t i = 0; i < conversion.materials.size(); ++i) {
          asset::Material& material = conversion.materials[i];
          const std::string& material_file =
              i < conversion.material_files.size() ? conversion.material_files[i] : std::string();
          if (material_file.empty()) continue;
          auto bytes = database.vfs().Read(material_file);
          if (!bytes) continue;
          BgsmMaterial parsed;
          if (!ParseBgsm(ByteSpan(bytes->data(), bytes->size()), &parsed)) continue;
          if (!parsed.diffuse.empty()) {
            material.base_color = asset::MakeAssetId(parsed.diffuse);
            database.LoadTexture(parsed.diffuse);
          }
          if (!parsed.normal.empty()) {
            material.normal = asset::MakeAssetId(parsed.normal);
            database.LoadTexture(parsed.normal);
          }
          if (parsed.roughness >= 0.0f) material.roughness_factor = parsed.roughness;
        }
        for (const asset::Material& material : conversion.materials) {
          database.AddMaterial(material);
        }
        if (conversion.skipped_shapes > 0) {
          REC_DEBUG("{}: skipped {} shapes", path, conversion.skipped_shapes);
        }
        if (conversion.skinned_shapes > 0) {
          REC_INFO("{}: baked {} skinned shapes", path, conversion.skinned_shapes);
        }
        return std::move(conversion.mesh);
      });
  database.RegisterTextureConverter(".dds", ConvertDds);
  if (profile.game == Game::kFallout4 || profile.game == Game::kFallout76) {
    database.RegisterMaterialConverter(".bgsm", ConvertBgsm);
    database.RegisterMaterialConverter(".bgem", ConvertBgsm);
  }
  REC_INFO("registered bethesda converters for {}", profile.name);
}

}  // namespace rec::bethesda
