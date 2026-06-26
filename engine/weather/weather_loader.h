#ifndef RECREATION_WEATHER_WEATHER_LOADER_H_
#define RECREATION_WEATHER_WEATHER_LOADER_H_

#include <unordered_map>
#include <utility>
#include <vector>

#include "bethesda/load_order.h"
#include "weather/weather.h"

namespace rec::weather {

// Parses every WTHR (weather) record into a def map keyed by packed form id, and
// returns the count. Defensive: the physical params come from the DATA
// classification flags (pleasant / cloudy / rainy / snow), with an editor-id
// fallback. Bethesda's baked sky/cloud textures in the record are ignored - only
// the parameters and the authored intent are kept.
int LoadWeathers(const bethesda::RecordStore& records, std::unordered_map<u64, WeatherDef>* out);

// Builds a weighted climate (the input a WeatherSystem expects) for the named
// worldspace: its authored CLMT weather list (WRLD CNAM -> CLMT WLST), else the
// CLMT with the most resolvable weathers, else a synthetic spread over the
// loaded weather kinds. Empty when no weathers loaded.
std::vector<std::pair<WeatherDef, u32>> BuildClimate(
    const bethesda::RecordStore& records, const std::unordered_map<u64, WeatherDef>& weathers,
    const char* worldspace_edid);

}  // namespace rec::weather

#endif  // RECREATION_WEATHER_WEATHER_LOADER_H_
