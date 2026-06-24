#ifndef RECREATION_BETHESDA_CONVERTERS_H_
#define RECREATION_BETHESDA_CONVERTERS_H_

#include <string>

#include "asset/asset_database.h"
#include "bethesda/game_profile.h"
#include "core/types.h"

namespace rec::bethesda {

// The subset of a Fallout 4 BGSM (lighting) / BGEM (effect) material file the
// engine uses: the base-colour and normal texture paths (normalized vfs paths;
// normal is empty for BGEM) and, for BGSM, the smoothness mapped to PBR
// roughness. roughness is negative when unknown, so a caller keeps its default.
struct BgsmMaterial {
  std::string diffuse;
  std::string normal;
  f32 roughness = -1.0f;
};

// Parses a version-2 BGSM/BGEM material file. Returns false for another version,
// a non-material blob, or a short read. Exposed for the asset layer and tests.
bool ParseBgsm(ByteSpan data, BgsmMaterial* out);

// Hooks the bethesda format converters into the asset database:
//   .nif        -> asset::Mesh
//   .dds        -> asset::Texture (BCn passthrough)
//   .bgsm/.bgem -> asset::Material (FO4/76 material files)
// SSE has no material files, its shader data lives in the NIF shader
// property blocks and is emitted as synthetic materials during NIF
// conversion.
void RegisterConverters(asset::AssetDatabase& database, const GameProfile& profile);

}  // namespace rec::bethesda

#endif  // RECREATION_BETHESDA_CONVERTERS_H_
