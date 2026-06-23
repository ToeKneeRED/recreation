#include "modstream/content_provider.h"

#include <fstream>
#include <unordered_map>

#include "asset/asset_id.h"

namespace rec::modstream {
namespace {

// Serves one resource's files out of the content store. The path index maps each
// resource-relative normalized path to the content hash that names its bytes;
// reads stream the cached file straight off disk.
class ContentStoreProvider final : public asset::FileProvider {
 public:
  ContentStoreProvider(std::string resource_name, const ModResource& resource,
                       const ContentStore& store)
      : name_(std::move(resource_name)), store_(store) {
    paths_.reserve(resource.files.size());
    for (const ResourceFile& file : resource.files) paths_.emplace(file.path, file.hash);
  }

  bool Contains(std::string_view normalized_path) const override {
    return paths_.find(std::string(normalized_path)) != paths_.end();
  }

  std::optional<base::Vector<u8>> Read(std::string_view normalized_path) const override {
    const auto it = paths_.find(std::string(normalized_path));
    if (it == paths_.end()) return std::nullopt;
    const std::optional<std::filesystem::path> path = store_.PathFor(it->second);
    if (!path) return std::nullopt;

    std::ifstream file(*path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    base::Vector<u8> data(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    if (!file) return std::nullopt;
    return data;
  }

  void Enumerate(const std::function<void(std::string_view)>& fn) const override {
    for (const auto& [path, hash] : paths_) fn(path);
  }

  std::string name() const override { return name_; }

 private:
  std::string name_;
  const ContentStore& store_;
  std::unordered_map<std::string, ContentHash> paths_;
};

}  // namespace

void MountManifest(asset::Vfs& vfs, const ModManifest& manifest,
                   const ContentStore& store) {
  for (const ModResource& resource : manifest.resources) {
    vfs.Mount(base::MakeUnique<ContentStoreProvider>(
        "modstream:" + resource.name, resource, store));
  }
}

}  // namespace rec::modstream
