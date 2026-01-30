#include "tile/dsm_provider.h"
#include "tile/geotiff.h"
#include "util/log.h"
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>

namespace mesh3d {

DSMProvider::DSMProvider()
    : m_cache("dsm") {}

mesh3d_bounds_t DSMProvider::coverage() const {
    return {-90.0, 90.0, -180.0, 180.0};
}

TileCoord DSMProvider::latlon_to_dsm_coord(double lat, double lon) {
    /* DSM tiles use z=-2 sentinel, 0.01-degree grid */
    return {static_cast<int>(std::floor(lon * 100.0)),
            static_cast<int>(std::floor(lat * 100.0)),
            -2};
}

mesh3d_bounds_t DSMProvider::dsm_tile_bounds(const TileCoord& coord) {
    double min_lon = coord.x / 100.0;
    double min_lat = coord.y / 100.0;
    return {min_lat, min_lat + 0.01, min_lon, min_lon + 0.01};
}

void DSMProvider::set_data_dir(const std::string& dir) {
    m_data_dir = dir;
    m_scanned = false;
}

void DSMProvider::set_url_template(const std::string& tmpl) {
    m_url_template = tmpl;
}

void DSMProvider::scan_directory() {
    if (m_scanned || m_data_dir.empty()) return;
    m_scanned = true;
    m_index.clear();

    namespace fs = std::filesystem;
    if (!fs::exists(m_data_dir)) {
        LOG_WARN("DSM directory does not exist: %s", m_data_dir.c_str());
        return;
    }

    int count = 0;
    for (auto& entry : fs::recursive_directory_iterator(m_data_dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".tif" && ext != ".tiff") continue;

        /* Read just the header to get bounds */
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file) continue;

        /* Read first 64KB for header parsing */
        std::vector<uint8_t> header(64 * 1024);
        file.read(reinterpret_cast<char*>(header.data()), header.size());
        size_t bytes_read = file.gcount();
        header.resize(bytes_read);

        GeoTiffInfo info;
        if (!geotiff_parse(header.data(), header.size(), info)) continue;
        if (!info.has_geo) continue;

        mesh3d_bounds_t bounds;
        bounds.min_lon = info.tie_x;
        bounds.max_lon = info.tie_x + info.scale_x * info.width;
        bounds.max_lat = info.tie_y;
        bounds.min_lat = info.tie_y - info.scale_y * info.height;

        TileCoord coord = latlon_to_dsm_coord(
            (bounds.min_lat + bounds.max_lat) * 0.5,
            (bounds.min_lon + bounds.max_lon) * 0.5);

        m_index.push_back({entry.path().string(), bounds, coord});
        ++count;
    }

    LOG_INFO("DSM: indexed %d GeoTIFF tiles in %s", count, m_data_dir.c_str());
}

std::vector<TileCoord> DSMProvider::tiles_in_bounds(const mesh3d_bounds_t& bounds, int /*zoom*/) const {
    const_cast<DSMProvider*>(this)->scan_directory();

    std::vector<TileCoord> result;
    for (auto& idx : m_index) {
        /* Check overlap */
        if (idx.bounds.max_lat < bounds.min_lat || idx.bounds.min_lat > bounds.max_lat)
            continue;
        if (idx.bounds.max_lon < bounds.min_lon || idx.bounds.min_lon > bounds.max_lon)
            continue;
        result.push_back(idx.coord);
    }
    return result;
}

std::vector<TileCoord> DSMProvider::tiles_in_view(double lat, double lon) const {
    const_cast<DSMProvider*>(this)->scan_directory();

    /* Find all indexed tiles within ~1km of the camera */
    double range_deg = 0.01; // ~1km
    mesh3d_bounds_t view_bounds = {
        lat - range_deg, lat + range_deg,
        lon - range_deg, lon + range_deg
    };
    return tiles_in_bounds(view_bounds, 0);
}

std::optional<TileData> DSMProvider::load_geotiff(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;

    size_t size = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> raw(size);
    file.read(reinterpret_cast<char*>(raw.data()), size);

    GeoTiffInfo info;
    if (!geotiff_parse(raw.data(), raw.size(), info)) {
        LOG_WARN("DSM: failed to parse GeoTIFF %s", path.c_str());
        return std::nullopt;
    }

    auto elev = geotiff_read_elevation(raw.data(), raw.size(), info);
    if (elev.empty()) {
        LOG_WARN("DSM: no elevation data in %s", path.c_str());
        return std::nullopt;
    }

    TileData td;
    td.elevation = std::move(elev);
    td.elev_rows = info.height;
    td.elev_cols = info.width;

    if (info.has_geo) {
        td.bounds.min_lon = info.tie_x;
        td.bounds.max_lon = info.tie_x + info.scale_x * info.width;
        td.bounds.max_lat = info.tie_y;
        td.bounds.min_lat = info.tie_y - info.scale_y * info.height;
    }

    td.coord = latlon_to_dsm_coord(
        (td.bounds.min_lat + td.bounds.max_lat) * 0.5,
        (td.bounds.min_lon + td.bounds.max_lon) * 0.5);

    LOG_INFO("DSM: loaded %s (%dx%d, %.6f-%.6f lat, %.6f-%.6f lon)",
             path.c_str(), info.width, info.height,
             td.bounds.min_lat, td.bounds.max_lat,
             td.bounds.min_lon, td.bounds.max_lon);

    return td;
}

std::optional<TileData> DSMProvider::fetch_tile(const TileCoord& coord) {
    scan_directory();

    /* Find matching tile in index */
    for (auto& idx : m_index) {
        if (idx.coord == coord) {
            return load_geotiff(idx.filepath);
        }
    }

    return std::nullopt;
}

} // namespace mesh3d
