#ifndef RECREATION_BETHESDA_STARFIELD_MESH_H_
#define RECREATION_BETHESDA_STARFIELD_MESH_H_

#include <string>

#include <base/containers/vector.h>

#include "asset/mesh.h"
#include "core/types.h"

namespace rec::bethesda {

// Starfield (BS stream 173) replaced the inline BSTriShape geometry with the
// BSGeometry block: it carries only a node transform, bounds, and a list of LOD
// references to external ".mesh" files (keyed by hash). The geometry itself
// lives in those ".mesh" files. The NIF parser resolves each BSGeometry into a
// baked object-space node transform plus the vfs path of its highest detail
// ".mesh"; the converter then loads and decodes that mesh.

// One decoded ".mesh" geometry buffer. Positions are in Bethesda object space
// (game units, ~70 per metre) after the unit conversion baked in on parse;
// normals are smooth area-weighted face normals (the packed mesh normals use an
// undecoded format); UVs come from the first texture coordinate stream.
struct StarfieldMeshData {
  base::Vector<asset::Vertex> vertices;
  base::Vector<u32> indices;
};

// Decodes a Starfield ".mesh" blob. Returns false on a short read, an
// unsupported version, or a stream the layout does not account for.
bool ParseStarfieldMesh(ByteSpan data, StarfieldMeshData* out);

// One BSGeometry resolved from a Starfield NIF: the full node-chain transform
// baked from the parents down to the geometry, and the normalized vfs path of
// its highest detail ".mesh" ("geometries/<dir>/<file>.mesh").
struct StarfieldGeometryRef {
  // Row-major 3x3 rotation, translation, uniform scale, matching the NIF
  // transform convention. Applied as rotation * p * scale + translation.
  f32 rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  f32 translation[3] = {0, 0, 0};
  f32 scale = 1.0f;
  std::string mesh_path;
};

// Walks a Starfield NIF node graph and collects every visible BSGeometry as a
// geometry reference with its baked world transform and resolved mesh path.
// Returns false when the header does not parse or no geometry is found.
bool ParseStarfieldNif(ByteSpan data, base::Vector<StarfieldGeometryRef>* out);

}  // namespace rec::bethesda

#endif  // RECREATION_BETHESDA_STARFIELD_MESH_H_
