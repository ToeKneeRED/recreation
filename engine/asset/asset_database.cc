#include "asset/asset_database.h"

#include "core/log.h"

namespace rec::asset {
namespace {

base::String ExtensionOf(std::string_view normalized_path) {
  size_t dot = normalized_path.rfind('.');
  if (dot == std::string_view::npos) return {};
  std::string_view extension = normalized_path.substr(dot);
  return base::String(extension.data(), extension.size());
}

template <typename Asset, typename Converter>
const Asset* LoadWith(Vfs& vfs, std::string_view path,
                      const base::UnorderedMap<base::String, Converter>& converters,
                      base::UnorderedMap<u64, base::UniquePointer<Asset>>& cache) {
  std::string normalized = NormalizePath(path);
  AssetId id = MakeAssetId(normalized);
  if (auto* cached = cache.find(id.hash)) return cached->Get_UseOnlyIfYouKnowWhatYouareDoing();

  const Converter* converter = converters.find(ExtensionOf(normalized));
  if (!converter) {
    REC_WARN("no converter for {}", normalized);
    return nullptr;
  }
  auto bytes = vfs.Read(normalized);
  if (!bytes) {
    REC_WARN("asset not found: {}", normalized);
    return nullptr;
  }
  auto asset = (*converter)(ByteSpan(bytes->data(), bytes->size()), id);
  if (!asset) {
    REC_WARN("conversion failed: {}", normalized);
    return nullptr;
  }
  return cache.emplace(id.hash, std::move(asset))
      .first->Get_UseOnlyIfYouKnowWhatYouareDoing();
}

}  // namespace

void AssetDatabase::RegisterMeshConverter(base::String extension, MeshConverter converter) {
  mesh_converters_.emplace(extension, std::move(converter));
}

void AssetDatabase::RegisterTextureConverter(base::String extension, TextureConverter converter) {
  texture_converters_.emplace(extension, std::move(converter));
}

void AssetDatabase::RegisterMaterialConverter(base::String extension, MaterialConverter converter) {
  material_converters_.emplace(extension, std::move(converter));
}

const Mesh* AssetDatabase::LoadMesh(std::string_view path) {
  return LoadWith(vfs_, path, mesh_converters_, meshes_);
}

const Texture* AssetDatabase::LoadTexture(std::string_view path) {
  return LoadWith(vfs_, path, texture_converters_, textures_);
}

const Material* AssetDatabase::LoadMaterial(std::string_view path) {
  return LoadWith(vfs_, path, material_converters_, materials_);
}

}  // namespace rec::asset
