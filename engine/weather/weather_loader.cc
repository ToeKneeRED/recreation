#include "weather/weather_loader.h"

#include <cctype>
#include <cstring>

#include "bethesda/record.h"
#include "core/log.h"

namespace rec::weather {
namespace {

constexpr u32 kWthr = FourCc('W', 'T', 'H', 'R');
constexpr u32 kClmt = FourCc('C', 'L', 'M', 'T');
constexpr u32 kEdid = FourCc('E', 'D', 'I', 'D');
constexpr u32 kData = FourCc('D', 'A', 'T', 'A');
constexpr u32 kWlst = FourCc('W', 'L', 'S', 'T');
constexpr u32 kCnam = FourCc('C', 'N', 'A', 'M');

// WTHR DATA classification flags byte (offset 11 in the Skyrim/FO4 layout).
WeatherDef::Kind Classify(const bethesda::Record& rec, const std::string& edid) {
  if (const bethesda::Subrecord* data = rec.Find(kData); data && data->data.size() >= 12) {
    u8 cls = data->data[11];
    if (cls & 0x08) return WeatherDef::Kind::kSnow;
    if (cls & 0x04) return WeatherDef::Kind::kRainy;
    if (cls & 0x02) return WeatherDef::Kind::kCloudy;
    if (cls & 0x01) return WeatherDef::Kind::kPleasant;
  }
  // Fallback: vanilla editor ids are descriptive (SkyrimRainStorm, ...).
  std::string l;
  for (char c : edid) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  auto has = [&](const char* s) { return l.find(s) != std::string::npos; };
  if (has("snow") || has("blizzard")) return WeatherDef::Kind::kSnow;
  if (has("rain") || has("storm") || has("thunder")) return WeatherDef::Kind::kRainy;
  if (has("cloud") || has("overcast") || has("fog")) return WeatherDef::Kind::kCloudy;
  return WeatherDef::Kind::kPleasant;
}

bool ReadFormRef(const bethesda::RecordStore& records, const bethesda::Record& rec, u32 sub,
                 u16 plugin, bethesda::GlobalFormId* out) {
  const bethesda::Subrecord* s = rec.Find(sub);
  if (!s || s->data.size() < 4) return false;
  u32 raw;
  std::memcpy(&raw, s->data.data(), 4);
  *out = records.ResolveFrom(bethesda::RawFormId{raw}, plugin);
  return out->plugin != 0xffff;
}

// A climate's WLST: 12-byte entries of (weather form id, chance u32, global).
std::vector<std::pair<WeatherDef, u32>> FromClimate(
    const bethesda::RecordStore& records, bethesda::GlobalFormId climate,
    const std::unordered_map<u64, WeatherDef>& weathers) {
  std::vector<std::pair<WeatherDef, u32>> out;
  const bethesda::RecordStore::StoredRecord* stored = records.Find(climate);
  bethesda::Record rec;
  if (!stored || !records.Parse(climate, &rec)) return out;
  const bethesda::Subrecord* wlst = rec.Find(kWlst);
  if (!wlst) return out;
  const u8* p = wlst->data.data();
  size_t sz = wlst->data.size();
  for (size_t off = 0; off + 12 <= sz; off += 12) {
    u32 raw, chance;
    std::memcpy(&raw, p + off, 4);
    std::memcpy(&chance, p + off + 4, 4);
    bethesda::GlobalFormId wid =
        records.ResolveFrom(bethesda::RawFormId{raw}, stored->winning_plugin);
    auto it = weathers.find(wid.packed());
    if (it != weathers.end() && chance > 0) out.push_back({it->second, chance});
  }
  return out;
}

std::vector<std::pair<WeatherDef, u32>> Synthetic(
    const std::unordered_map<u64, WeatherDef>& weathers) {
  const WeatherDef* rep[4] = {nullptr, nullptr, nullptr, nullptr};
  for (auto& [id, def] : weathers) {
    int k = static_cast<int>(def.kind);
    if (k >= 0 && k < 4 && !rep[k]) rep[k] = &def;
  }
  const u32 weight[4] = {50, 30, 12, 8};  // pleasant, cloudy, rainy, snow
  std::vector<std::pair<WeatherDef, u32>> out;
  for (int k = 0; k < 4; ++k)
    if (rep[k]) out.push_back({*rep[k], weight[k]});
  return out;
}

}  // namespace

int LoadWeathers(const bethesda::RecordStore& records, std::unordered_map<u64, WeatherDef>* out) {
  int n = 0;
  records.EachOfType(
      kWthr, [&](bethesda::GlobalFormId id, const bethesda::RecordStore::StoredRecord&) {
        bethesda::Record rec;
        if (!records.Parse(id, &rec)) return;
        WeatherDef def;
        def.form = id.packed();
        def.editor_id = rec.GetString(kEdid);
        def.kind = Classify(rec, def.editor_id);
        def.DeriveFromKind();
        if (const bethesda::Subrecord* d = rec.Find(kData); d && d->data.size() >= 1)
          def.wind = static_cast<f32>(d->data[0]) / 255.0f * 30.0f;  // byte -> m/s
        (*out)[def.form] = std::move(def);
        ++n;
      });
  return n;
}

std::vector<std::pair<WeatherDef, u32>> BuildClimate(
    const bethesda::RecordStore& records, const std::unordered_map<u64, WeatherDef>& weathers,
    const char* worldspace_edid) {
  if (weathers.empty()) return {};

  // 1) The worldspace's authored climate (WRLD CNAM -> CLMT).
  bethesda::GlobalFormId wrld = records.FindWorldspace(worldspace_edid);
  if (wrld.plugin != 0xffff) {
    bethesda::Record wrec;
    const bethesda::RecordStore::StoredRecord* ws = records.Find(wrld);
    if (ws && records.Parse(wrld, &wrec)) {
      bethesda::GlobalFormId climate;
      if (ReadFormRef(records, wrec, kCnam, ws->winning_plugin, &climate)) {
        auto list = FromClimate(records, climate, weathers);
        if (list.size() >= 2) {  // a real spread, not a partly-parsed list
          REC_INFO("weather: climate from worldspace {} ({} weathers)", worldspace_edid,
                   list.size());
          return list;
        }
      }
    }
  }

  // 2) The CLMT with the most resolvable weathers.
  std::vector<std::pair<WeatherDef, u32>> best;
  records.EachOfType(kClmt,
                     [&](bethesda::GlobalFormId id, const bethesda::RecordStore::StoredRecord&) {
                       auto list = FromClimate(records, id, weathers);
                       if (list.size() > best.size()) best = std::move(list);
                     });
  if (best.size() >= 2) {
    REC_INFO("weather: climate from largest CLMT ({} weathers)", best.size());
    return best;
  }

  // 3) Synthetic spread over the loaded weather kinds.
  auto syn = Synthetic(weathers);
  REC_INFO("weather: synthetic climate ({} kinds)", syn.size());
  return syn;
}

}  // namespace rec::weather
