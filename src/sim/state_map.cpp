#include "sim/state_map.h"

#include "loader/mod_fs.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace sim {

namespace {

uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t rd16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

} // namespace

bool StateMap::build(const std::string& mod_root, const world::World& w, int downscale, std::string& error) {
    if (downscale < 1) downscale = 1;

    // definition.csv: province id;r;g;b;...
    std::unordered_map<uint32_t, int32_t> color_to_province;
    {
        std::ifstream in(loader::join(mod_root, "map/definition.csv"));
        if (!in) {
            error = "cannot read map/definition.csv";
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            int id = 0, r = 0, g = 0, b = 0;
            if (std::sscanf(line.c_str(), "%d;%d;%d;%d", &id, &r, &g, &b) == 4 && id > 0) {
                color_to_province[(static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b)] = id;
            }
        }
    }

    // province -> dense state index (loaded states only)
    std::unordered_map<int32_t, uint16_t> province_to_index;
    index_to_state.assign(1, -1);
    for (int32_t sid : w.existing_states()) {
        const world::State* st = w.state(sid);
        if (!st || st->provinces.empty()) continue;
        const uint16_t idx = static_cast<uint16_t>(index_to_state.size());
        index_to_state.push_back(sid);
        for (int32_t p : st->provinces) province_to_index[p] = idx;
    }

    // provinces.bmp (24-bit, uncompressed)
    std::FILE* f = std::fopen(loader::join(mod_root, "map/provinces.bmp").c_str(), "rb");
    if (!f) {
        error = "cannot read map/provinces.bmp";
        return false;
    }
    std::vector<unsigned char> data;
    std::fseek(f, 0, SEEK_END);
    data.resize(static_cast<size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    const size_t got = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    if (got < 54 || data[0] != 'B' || data[1] != 'M') {
        error = "provinces.bmp is not a BMP";
        return false;
    }
    const uint32_t offset = rd32(&data[10]);
    const int32_t src_w = static_cast<int32_t>(rd32(&data[18]));
    const int32_t raw_h = static_cast<int32_t>(rd32(&data[22]));
    const uint16_t bpp = rd16(&data[28]);
    if (bpp != 24 || src_w <= 0 || raw_h == 0) {
        error = "provinces.bmp must be 24-bit";
        return false;
    }
    const bool bottom_up = raw_h > 0;
    const int32_t src_h = bottom_up ? raw_h : -raw_h;
    const size_t stride = (static_cast<size_t>(src_w) * 3 + 3) & ~static_cast<size_t>(3);
    if (offset + stride * static_cast<size_t>(src_h) > data.size()) {
        error = "provinces.bmp is truncated";
        return false;
    }

    width = src_w / downscale;
    height = src_h / downscale;
    pixel_index.assign(static_cast<size_t>(width) * height, 0);
    sum_x.assign(index_to_state.size(), 0);
    sum_y.assign(index_to_state.size(), 0);
    area.assign(index_to_state.size(), 0);
    uint32_t last_color = 0xFFFFFFFFu;
    uint16_t last_index = 0;
    for (int y = 0; y < height; ++y) {
        const int sy = y * downscale;
        const int file_row = bottom_up ? (src_h - 1 - sy) : sy;
        const unsigned char* row = &data[offset + stride * static_cast<size_t>(file_row)];
        for (int x = 0; x < width; ++x) {
            const unsigned char* px = row + static_cast<size_t>(x * downscale) * 3;
            const uint32_t color = (static_cast<uint32_t>(px[2]) << 16) | (static_cast<uint32_t>(px[1]) << 8) | px[0];
            if (color != last_color) {
                last_color = color;
                last_index = 0;
                auto pit = color_to_province.find(color);
                if (pit != color_to_province.end()) {
                    auto sit = province_to_index.find(pit->second);
                    if (sit != province_to_index.end()) last_index = sit->second;
                }
            }
            pixel_index[static_cast<size_t>(y) * width + x] = last_index;
            if (last_index) {
                sum_x[last_index] += x;
                sum_y[last_index] += y;
                ++area[last_index];
            }
        }
    }
    return true;
}

int StateMap::state_at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return -1;
    const uint16_t idx = pixel_index[static_cast<size_t>(y) * width + x];
    return idx ? index_to_state[idx] : -1;
}

bool StateMap::centroid(int32_t state_id, double& x, double& y) const {
    for (size_t i = 1; i < index_to_state.size(); ++i) {
        if (index_to_state[i] == state_id && area[i] > 0) {
            x = static_cast<double>(sum_x[i]) / static_cast<double>(area[i]);
            y = static_cast<double>(sum_y[i]) / static_cast<double>(area[i]);
            return true;
        }
    }
    return false;
}

} // namespace sim
