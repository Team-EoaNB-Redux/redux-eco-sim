#pragma once

// State map for the UI: map/provinces.bmp + map/definition.csv + the loaded states' province lists become a
// per-pixel dense state index (0 = sea / not loaded). The renderer encodes that index into an RGB8 ID texture
// and colours it through a 1-pixel palette row per map mode (the Rota Fortunae technique), so switching map
// modes only rewrites the palette.

#include "world/world.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

struct StateMap {
    int width = 0;
    int height = 0;
    std::vector<uint16_t> pixel_index;   // width*height, row-major, top row first
    std::vector<int32_t> index_to_state; // index -> state id (index 0 = none)
    std::vector<int64_t> sum_x, sum_y, area;

    // `downscale` >= 1 samples every Nth pixel (the 5632x2048 source at 2 -> 2816x1024).
    bool build(const std::string& mod_root, const world::World& w, int downscale, std::string& error);
    int state_at(int x, int y) const;
    bool centroid(int32_t state_id, double& x, double& y) const;
};

} // namespace sim
