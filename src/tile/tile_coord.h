#pragma once
#include <mesh3d/types.h>
#include <vector>
#include <cmath>
#include <functional>

namespace mesh3d {

struct TileCoord {
    int z, x, y;

    bool operator==(const TileCoord& o) const {
        return z == o.z && x == o.x && y == o.y;
    }
    bool operator<(const TileCoord& o) const {
        if (z != o.z) return z < o.z;
        if (x != o.x) return x < o.x;
        return y < o.y;
    }
};

} // namespace mesh3d

/* std::hash specialization */
namespace std {
template<> struct hash<mesh3d::TileCoord> {
    size_t operator()(const mesh3d::TileCoord& c) const {
        size_t h = 0;
        h ^= std::hash<int>()(c.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(c.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(c.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
} // namespace std

namespace mesh3d {

/* Convert latitude to slippy map tile Y at zoom level z */
inline int lat_to_tile_y(double lat, int z) {
    double lat_rad = lat * M_PI / 180.0;
    int n = 1 << z;
    int y = static_cast<int>((1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / M_PI) / 2.0 * n);
    return std::clamp(y, 0, n - 1);
}

/* Convert longitude to slippy map tile X at zoom level z */
inline int lon_to_tile_x(double lon, int z) {
    int n = 1 << z;
    int x = static_cast<int>((lon + 180.0) / 360.0 * n);
    return std::clamp(x, 0, n - 1);
}

/* Get the geographic bounds of a slippy map tile */
inline mesh3d_bounds_t tile_bounds(const TileCoord& tc) {
    int n = 1 << tc.z;
    double lon_min = tc.x / static_cast<double>(n) * 360.0 - 180.0;
    double lon_max = (tc.x + 1) / static_cast<double>(n) * 360.0 - 180.0;

    double lat_max_rad = std::atan(std::sinh(M_PI * (1.0 - 2.0 * tc.y / static_cast<double>(n))));
    double lat_min_rad = std::atan(std::sinh(M_PI * (1.0 - 2.0 * (tc.y + 1) / static_cast<double>(n))));

    mesh3d_bounds_t b;
    b.min_lat = lat_min_rad * 180.0 / M_PI;
    b.max_lat = lat_max_rad * 180.0 / M_PI;
    b.min_lon = lon_min;
    b.max_lon = lon_max;
    return b;
}

/* Fractional tile coordinate from latitude (for sub-tile pixel mapping) */
inline double lat_to_tile_y_frac(double lat, int z) {
    double lat_rad = lat * M_PI / 180.0;
    double n = static_cast<double>(1 << z);
    return (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / M_PI) / 2.0 * n;
}

/* Fractional tile coordinate from longitude (for sub-tile pixel mapping) */
inline double lon_to_tile_x_frac(double lon, int z) {
    double n = static_cast<double>(1 << z);
    return (lon + 180.0) / 360.0 * n;
}

/* Get all tile coordinates covering a bounding box at a given zoom level */
inline std::vector<TileCoord> bounds_to_tile_range(const mesh3d_bounds_t& bounds, int zoom) {
    int x_min = lon_to_tile_x(bounds.min_lon, zoom);
    int x_max = lon_to_tile_x(bounds.max_lon, zoom);
    int y_min = lat_to_tile_y(bounds.max_lat, zoom); // max_lat -> smaller y
    int y_max = lat_to_tile_y(bounds.min_lat, zoom); // min_lat -> larger y

    std::vector<TileCoord> tiles;
    for (int y = y_min; y <= y_max; ++y) {
        for (int x = x_min; x <= x_max; ++x) {
            tiles.push_back({zoom, x, y});
        }
    }
    return tiles;
}

} // namespace mesh3d
