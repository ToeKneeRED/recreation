#include "render/geometry/hair_groom.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <base/containers/unordered_map.h>

#include "core/log.h"

namespace rec::render {
namespace {

// Bethesda object space (Z-up, game units) -> engine space (Y-up, metres). Same
// mapping the cell streamer uses: engine = (x, z, -y) * units_to_metres.
constexpr f32 kUnitsToMeters = 0.01428f;

Vec3 ToEngine(const f32 p[3]) {
  return {p[0] * kUnitsToMeters, p[2] * kUnitsToMeters, -p[1] * kUnitsToMeters};
}

struct GVert {
  Vec3 p;
  f32 u = 0, v = 0;
};

struct Tri {
  u32 a, b, c;
};

// --- CPU diffuse sampling (BC1/BC3/RGBA8) --------------------------------

struct DecodedTex {
  u32 w = 0, h = 0;
  base::Vector<f32> rgb;  // linear, row major, 3 per texel
};

void DecodeBc1Colors(const u8* block, bool bc3, u8 out[16][3]) {
  u16 c0, c1;
  std::memcpy(&c0, block, 2);
  std::memcpy(&c1, block + 2, 2);
  auto expand = [](u16 c, u8 rgb[3]) {
    rgb[0] = static_cast<u8>(((c >> 11) & 0x1f) * 255 / 31);
    rgb[1] = static_cast<u8>(((c >> 5) & 0x3f) * 255 / 63);
    rgb[2] = static_cast<u8>((c & 0x1f) * 255 / 31);
  };
  u8 pal[4][3];
  expand(c0, pal[0]);
  expand(c1, pal[1]);
  bool four = bc3 || c0 > c1;
  for (int k = 0; k < 3; ++k) {
    if (four) {
      pal[2][k] = static_cast<u8>((2 * pal[0][k] + pal[1][k]) / 3);
      pal[3][k] = static_cast<u8>((pal[0][k] + 2 * pal[1][k]) / 3);
    } else {
      pal[2][k] = static_cast<u8>((pal[0][k] + pal[1][k]) / 2);
      pal[3][k] = 0;
    }
  }
  u32 bits;
  std::memcpy(&bits, block + 4, 4);
  for (int i = 0; i < 16; ++i) {
    u32 idx = (bits >> (i * 2)) & 0x3;
    for (int k = 0; k < 3; ++k) out[i][k] = pal[idx][k];
  }
}

size_t MipOffset(const asset::Texture& t, u32 mip, u32* w, u32* h) {
  bool compressed = t.format != asset::TextureFormat::kRgba8;
  size_t block = (t.format == asset::TextureFormat::kBc1) ? 8 : 16;
  size_t offset = 0;
  for (u32 m = 0; m < mip; ++m) {
    u32 mw = std::max(1u, t.width >> m);
    u32 mh = std::max(1u, t.height >> m);
    offset += compressed ? ((mw + 3) / 4) * ((mh + 3) / 4) * block
                         : static_cast<size_t>(mw) * mh * 4;
  }
  *w = std::max(1u, t.width >> mip);
  *h = std::max(1u, t.height >> mip);
  return offset;
}

bool DecodeDiffuse(const asset::Texture& t, u32 target_max, DecodedTex* out) {
  if (t.format != asset::TextureFormat::kBc1 && t.format != asset::TextureFormat::kBc3 &&
      t.format != asset::TextureFormat::kRgba8) {
    return false;
  }
  u32 mip = 0;
  for (u32 m = 0; m + 1 < t.mip_count; ++m) {
    if (std::max(t.width >> m, t.height >> m) <= target_max) break;
    mip = m + 1;
  }
  u32 w, h;
  size_t offset = MipOffset(t, mip, &w, &h);
  out->w = w;
  out->h = h;
  out->rgb.resize(static_cast<size_t>(w) * h * 3);
  auto to_lin = [&](u8 c) {
    f32 x = c / 255.0f;
    return t.is_srgb ? std::pow(x, 2.2f) : x;
  };
  if (t.format == asset::TextureFormat::kRgba8) {
    if (offset + static_cast<size_t>(w) * h * 4 > t.data.size()) return false;
    for (u32 i = 0; i < w * h; ++i) {
      for (int k = 0; k < 3; ++k) out->rgb[i * 3 + k] = to_lin(t.data[offset + i * 4 + k]);
    }
    return true;
  }
  bool bc3 = t.format == asset::TextureFormat::kBc3;
  size_t block_size = bc3 ? 16 : 8;
  u32 bw = (w + 3) / 4, bh = (h + 3) / 4;
  if (offset + static_cast<size_t>(bw) * bh * block_size > t.data.size()) return false;
  for (u32 by = 0; by < bh; ++by) {
    for (u32 bx = 0; bx < bw; ++bx) {
      const u8* blk = t.data.data() + offset + (static_cast<size_t>(by) * bw + bx) * block_size;
      u8 colors[16][3];
      DecodeBc1Colors(blk + (bc3 ? 8 : 0), bc3, colors);
      for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
          u32 x = bx * 4 + px, y = by * 4 + py;
          if (x >= w || y >= h) continue;
          const u8* c = colors[py * 4 + px];
          f32* dst = &out->rgb[(static_cast<size_t>(y) * w + x) * 3];
          for (int k = 0; k < 3; ++k) dst[k] = to_lin(c[k]);
        }
      }
    }
  }
  return true;
}

Vec3 SampleTex(const DecodedTex& t, f32 u, f32 v) {
  auto wrap = [](f32 x) { return x - std::floor(x); };
  f32 fx = wrap(u) * t.w - 0.5f, fy = wrap(v) * t.h - 0.5f;
  i32 x0 = static_cast<i32>(std::floor(fx)), y0 = static_cast<i32>(std::floor(fy));
  f32 tx = fx - x0, ty = fy - y0;
  auto at = [&](i32 x, i32 y) {
    x = ((x % (i32)t.w) + t.w) % t.w;
    y = ((y % (i32)t.h) + t.h) % t.h;
    const f32* p = &t.rgb[(static_cast<size_t>(y) * t.w + x) * 3];
    return Vec3{p[0], p[1], p[2]};
  };
  Vec3 a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
  Vec3 top = a + (b - a) * tx;
  Vec3 bot = c + (d - c) * tx;
  return top + (bot - top) * ty;
}

// --- union-find -----------------------------------------------------------

u32 Find(base::Vector<u32>& parent, u32 x) {
  while (parent[x] != x) {
    parent[x] = parent[parent[x]];
    x = parent[x];
  }
  return x;
}
void Union(base::Vector<u32>& parent, u32 a, u32 b) {
  a = Find(parent, a);
  b = Find(parent, b);
  if (a != b) parent[a] = b;
}

// A connected group of hair cards sharing vertices, with its length ("along")
// UV axis resolved. The scalp end is `a_root`, the tip end `a_tip`.
struct Comp {
  base::Vector<u32> verts;
  bool along_is_u = false;
  f32 a_root = 0, a_tip = 0;
  f32 c_min = 0, c_max = 0;
  f32 hw = 0.02f;  // world proximity bandwidth for the tracing chain
};

inline f32 AlongOf(const Comp& comp, const GVert& v) { return comp.along_is_u ? v.u : v.v; }
inline f32 AcrossOf(const Comp& comp, const GVert& v) { return comp.along_is_u ? v.v : v.u; }

// One point of a strand: a scattered-data average of the component's vertices
// weighted by along-distance, across-distance and world proximity to the
// previous point, so the chain follows one card strip even through welded
// meshes (parallel strips at the same UV are separated by the world term).
Vec3 ChainStep(const Comp& comp, const base::Vector<GVert>& verts, f32 target_a, f32 c,
               const Vec3& prev, f32 ha, f32 hc) {
  Vec3 acc{0, 0, 0};
  f32 wsum = 0;
  f32 inv_ha = 1.0f / (ha * ha);
  f32 inv_hc = 1.0f / (hc * hc);
  f32 inv_hw = 1.0f / (comp.hw * comp.hw);
  for (u32 vi : comp.verts) {
    const GVert& gv = verts[vi];
    f32 da = AlongOf(comp, gv) - target_a;
    f32 dc = AcrossOf(comp, gv) - c;
    Vec3 dw = gv.p - prev;
    f32 e = da * da * inv_ha + dc * dc * inv_hc + Dot(dw, dw) * inv_hw;
    f32 w = std::exp(-e);
    acc = acc + gv.p * w;
    wsum += w;
  }
  return wsum > 1e-12f ? acc * (1.0f / wsum) : prev;
}

}  // namespace

bool BuildHairGroom(const asset::Mesh& mesh, const GroomParams& params, GroomData* out) {
  if (mesh.lods.empty()) return false;
  const asset::MeshLod& lod = mesh.lods[0];
  if (lod.vertices.empty() || lod.indices.size() < 3) return false;

  base::Vector<GVert> verts;
  verts.resize(lod.vertices.size());
  for (size_t i = 0; i < lod.vertices.size(); ++i) {
    verts[i].p = ToEngine(lod.vertices[i].position);
    verts[i].u = lod.vertices[i].uv[0];
    verts[i].v = lod.vertices[i].uv[1];
  }
  base::Vector<Tri> tris;
  tris.reserve(lod.indices.size() / 3);
  for (size_t i = 0; i + 2 < lod.indices.size(); i += 3) {
    Tri t{lod.indices[i], lod.indices[i + 1], lod.indices[i + 2]};
    if (t.a == t.b || t.b == t.c || t.a == t.c) continue;
    tris.push_back(t);
  }
  if (tris.empty()) return false;

  // Connected components over shared vertices = individual cards / clumps.
  base::Vector<u32> parent;
  parent.resize(verts.size());
  for (u32 i = 0; i < verts.size(); ++i) parent[i] = i;
  for (const Tri& t : tris) {
    Union(parent, t.a, t.b);
    Union(parent, t.a, t.c);
  }
  base::UnorderedMap<u32, u32> root_to_comp;
  base::Vector<Comp> comps;
  base::Vector<u32> tri_comp;
  tri_comp.resize(tris.size());
  for (u32 ti = 0; ti < tris.size(); ++ti) {
    u32 root = Find(parent, tris[ti].a);
    u32* found = root_to_comp.find(root);
    u32 ci;
    if (found) {
      ci = *found;
    } else {
      ci = static_cast<u32>(comps.size());
      comps.push_back(Comp{});
      root_to_comp.emplace(root, ci);
    }
    tri_comp[ti] = ci;
  }
  // Collect each component's unique vertices.
  base::Vector<u32> vert_comp;
  vert_comp.resize(verts.size());
  for (u32 i = 0; i < verts.size(); ++i) vert_comp[i] = *root_to_comp.find(Find(parent, i));
  for (u32 i = 0; i < verts.size(); ++i) comps[vert_comp[i]].verts.push_back(i);

  // Per component: length axis, scalp/tip ends, across range, world bandwidth.
  for (u32 comp_index = 0; comp_index < comps.size(); ++comp_index) {
    Comp& comp = comps[comp_index];
    if (comp.verts.empty()) continue;
    Vec3 dpdu_sum{0, 0, 0}, dpdv_sum{0, 0, 0};
    Vec3 bmin{1e30f, 1e30f, 1e30f}, bmax{-1e30f, -1e30f, -1e30f};
    for (u32 vi : comp.verts) {
      const Vec3& p = verts[vi].p;
      bmin = {std::min(bmin.x, p.x), std::min(bmin.y, p.y), std::min(bmin.z, p.z)};
      bmax = {std::max(bmax.x, p.x), std::max(bmax.y, p.y), std::max(bmax.z, p.z)};
    }
    comp.hw = std::max(0.01f, Length(bmax - bmin) * 0.12f);
    // UV->world gradients over the component tris to pick the length axis.
    for (u32 ti = 0; ti < tris.size(); ++ti) {
      if (tri_comp[ti] != comp_index) continue;
      const Tri& t = tris[ti];
      const GVert& v0 = verts[t.a];
      const GVert& v1 = verts[t.b];
      const GVert& v2 = verts[t.c];
      Vec3 e1 = v1.p - v0.p, e2 = v2.p - v0.p;
      f32 area = Length(Cross(e1, e2)) * 0.5f;
      f32 du1 = v1.u - v0.u, dv1 = v1.v - v0.v;
      f32 du2 = v2.u - v0.u, dv2 = v2.v - v0.v;
      f32 det = du1 * dv2 - du2 * dv1;
      if (std::fabs(det) < 1e-12f) continue;
      dpdu_sum = dpdu_sum + (e1 * dv2 - e2 * dv1) * (area / det);
      dpdv_sum = dpdv_sum + (e2 * du1 - e1 * du2) * (area / det);
    }
    comp.along_is_u = Length(dpdu_sum) > Length(dpdv_sum);
    f32 amin = 1e30f, amax = -1e30f, cmin = 1e30f, cmax = -1e30f;
    for (u32 vi : comp.verts) {
      f32 a = AlongOf(comp, verts[vi]);
      f32 c = AcrossOf(comp, verts[vi]);
      amin = std::min(amin, a); amax = std::max(amax, a);
      cmin = std::min(cmin, c); cmax = std::max(cmax, c);
    }
    comp.c_min = cmin;
    comp.c_max = cmax;
    // Root = the along-end that sits higher up (the scalp): compare mean Y of
    // verts near each end.
    f32 y_lo = 0, y_hi = 0;
    u32 n_lo = 0, n_hi = 0;
    f32 band = 0.2f * (amax - amin) + 1e-6f;
    for (u32 vi : comp.verts) {
      f32 a = AlongOf(comp, verts[vi]);
      if (a < amin + band) { y_lo += verts[vi].p.y; ++n_lo; }
      if (a > amax - band) { y_hi += verts[vi].p.y; ++n_hi; }
    }
    y_lo = n_lo ? y_lo / n_lo : 0;
    y_hi = n_hi ? y_hi / n_hi : 0;
    if (y_lo >= y_hi) { comp.a_root = amin; comp.a_tip = amax; }
    else { comp.a_root = amax; comp.a_tip = amin; }
  }

  // Area-weighted seeding over ALL triangles (uniform surface coverage; no
  // per-card clumping). Build a cumulative area table.
  base::Vector<f32> cum;
  cum.resize(tris.size());
  f32 total_area = 0;
  for (u32 ti = 0; ti < tris.size(); ++ti) {
    const Tri& t = tris[ti];
    total_area += Length(Cross(verts[t.b].p - verts[t.a].p, verts[t.c].p - verts[t.a].p)) * 0.5f;
    cum[ti] = total_area;
  }
  if (total_area <= 0) return false;

  DecodedTex diffuse;
  bool have_tex = params.diffuse && DecodeDiffuse(*params.diffuse, 256, &diffuse);

  u32 rng = params.seed * 2654435761u + 1u;
  auto randf = [&]() {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<f32>(rng >> 8) / 16777216.0f;
  };

  const u32 P = kGroomPointsPerStrand;
  base::Vector<f32>& pts = out->points;
  base::Vector<f32>& roots = out->roots;
  base::Vector<f32>& cols = out->colors;
  pts.resize(static_cast<size_t>(params.guide_count) * P * 3);
  roots.resize(static_cast<size_t>(params.guide_count) * 3);
  cols.resize(static_cast<size_t>(params.guide_count) * 3);

  Vec3 root_sum{0, 0, 0};
  f64 length_sum = 0;
  u32 emitted = 0;
  for (u32 s = 0; s < params.guide_count; ++s) {
    // Pick a triangle by area, then a random point on it.
    f32 r = randf() * total_area;
    u32 lo = 0, hi = static_cast<u32>(tris.size()) - 1;
    while (lo < hi) {
      u32 mid = (lo + hi) / 2;
      if (cum[mid] < r) lo = mid + 1; else hi = mid;
    }
    const Tri& t = tris[lo];
    f32 b1 = randf(), b2 = randf();
    if (b1 + b2 > 1) { b1 = 1 - b1; b2 = 1 - b2; }
    f32 b0 = 1 - b1 - b2;
    const GVert& v0 = verts[t.a];
    const GVert& v1 = verts[t.b];
    const GVert& v2 = verts[t.c];
    Vec3 seed = v0.p * b0 + v1.p * b1 + v2.p * b2;
    const Comp& comp = comps[tri_comp[lo]];
    f32 c_seed = AcrossOf(comp, v0) * b0 + AcrossOf(comp, v1) * b1 + AcrossOf(comp, v2) * b2;
    if (comp.a_root == comp.a_tip) continue;

    f32 ha = 0.8f * std::fabs(comp.a_tip - comp.a_root) / (P - 1) + 1e-4f;
    f32 hc = 0.10f * std::fabs(comp.c_max - comp.c_min) + 1e-4f;
    // Trace root->tip; the chain starts anchored on the seed's strip.
    Vec3 prev = seed;
    Vec3 pt_prev{};
    f32 len = 0;
    Vec3 root_pos{};
    for (u32 k = 0; k < P; ++k) {
      f32 tt = static_cast<f32>(k) / (P - 1);
      f32 a = comp.a_root + (comp.a_tip - comp.a_root) * tt;
      Vec3 pos = ChainStep(comp, verts, a, c_seed, prev, ha, hc);
      prev = pos;
      pts[(static_cast<size_t>(s) * P + k) * 3 + 0] = pos.x;
      pts[(static_cast<size_t>(s) * P + k) * 3 + 1] = pos.y;
      pts[(static_cast<size_t>(s) * P + k) * 3 + 2] = pos.z;
      if (k == 0) {
        root_pos = pos;
        roots[s * 3 + 0] = pos.x;
        roots[s * 3 + 1] = pos.y;
        roots[s * 3 + 2] = pos.z;
      } else {
        len += Length(pos - pt_prev);
      }
      pt_prev = pos;
    }
    root_sum = root_sum + root_pos;
    f32 root_u = comp.along_is_u ? comp.a_root : c_seed;
    f32 root_v = comp.along_is_u ? c_seed : comp.a_root;
    Vec3 color = have_tex ? SampleTex(diffuse, root_u, root_v) : Vec3{1, 1, 1};
    cols[emitted * 3 + 0] = color.x;
    cols[emitted * 3 + 1] = color.y;
    cols[emitted * 3 + 2] = color.z;
    // Compact into slot `emitted` (skipped seeds leave gaps otherwise).
    if (emitted != s) {
      for (u32 k = 0; k < P * 3; ++k) pts[emitted * P * 3 + k] = pts[s * P * 3 + k];
      for (u32 k = 0; k < 3; ++k) roots[emitted * 3 + k] = roots[s * 3 + k];
    }
    length_sum += len;
    ++emitted;
  }
  if (emitted == 0) return false;
  pts.resize(static_cast<size_t>(emitted) * P * 3);
  roots.resize(static_cast<size_t>(emitted) * 3);
  cols.resize(static_cast<size_t>(emitted) * 3);

  // Recentre on the scalp (mean root) so a rigid transform drops the groom onto
  // a head bone, and fit a head collision sphere just inside the scalp cap.
  Vec3 scalp = root_sum * (1.0f / emitted);
  f32 head_r = 0;
  for (u32 i = 0; i < emitted; ++i) {
    Vec3 rp{roots[i * 3], roots[i * 3 + 1], roots[i * 3 + 2]};
    f32 horiz = std::sqrt((rp.x - scalp.x) * (rp.x - scalp.x) + (rp.z - scalp.z) * (rp.z - scalp.z));
    head_r = std::max(head_r, horiz);
  }
  for (size_t i = 0; i < pts.size(); i += 3) {
    pts[i] -= scalp.x; pts[i + 1] -= scalp.y; pts[i + 2] -= scalp.z;
  }
  for (size_t i = 0; i < roots.size(); i += 3) {
    roots[i] -= scalp.x; roots[i + 1] -= scalp.y; roots[i + 2] -= scalp.z;
  }
  out->guide_count = emitted;
  out->collision_center = {0.0f, -0.35f * head_r, 0.0f};
  out->collision_radius = 0.78f * head_r;
  out->mean_length = static_cast<f32>(length_sum / emitted);
  REC_INFO("hair groom: {} cards, {} guides, mean len {:.3f}, head r {:.3f}", comps.size(),
           emitted, out->mean_length, head_r);
  return true;
}

}  // namespace rec::render
