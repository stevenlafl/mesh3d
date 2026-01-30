#include "tile/hgt_provider.h"
#include "util/log.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <curl/curl.h>
#include <zlib.h>

namespace mesh3d {

HgtProvider::HgtProvider()
    : m_cache([] {
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + "/.cache/mesh3d/hgt";
        return std::string("/tmp/mesh3d/hgt");
    }())
{
    LOG_INFO("HGT provider: cache: %s", m_cache.cache_dir().c_str());
}

mesh3d_bounds_t HgtProvider::coverage() const {
    return {-90.0, 90.0, -180.0, 180.0};
}

TileCoord HgtProvider::latlon_to_hgt_coord(double lat, double lon) {
    int y = static_cast<int>(std::floor(lat));
    int x = static_cast<int>(std::floor(lon));
    return {-1, x, y};
}

std::string HgtProvider::coord_to_filename(const TileCoord& coord) {
    char buf[32];
    char ns = coord.y >= 0 ? 'N' : 'S';
    char ew = coord.x >= 0 ? 'E' : 'W';
    int lat_abs = std::abs(coord.y);
    int lon_abs = std::abs(coord.x);
    std::snprintf(buf, sizeof(buf), "%c%02d%c%03d.hgt", ns, lat_abs, ew, lon_abs);
    return buf;
}

mesh3d_bounds_t HgtProvider::hgt_tile_bounds(const TileCoord& coord) {
    mesh3d_bounds_t b;
    b.min_lat = coord.y;
    b.max_lat = coord.y + 1.0;
    b.min_lon = coord.x;
    b.max_lon = coord.x + 1.0;
    return b;
}

std::vector<TileCoord> HgtProvider::tiles_in_view(double lat, double lon) const {
    TileCoord center = latlon_to_hgt_coord(lat, lon);
    std::vector<TileCoord> tiles;
    tiles.reserve(4);

    // Always load the tile the camera is in
    tiles.push_back(center);

    // Fractional position within the tile (0..1)
    double frac_lat = lat - std::floor(lat);
    double frac_lon = lon - std::floor(lon);
    // Handle negative coordinates: frac should still be 0..1
    if (frac_lat < 0) frac_lat += 1.0;
    if (frac_lon < 0) frac_lon += 1.0;

    // If near a lat edge, load the adjacent lat tile
    static constexpr double EDGE_THRESH = 0.15; // ~17km at equator
    bool near_south = frac_lat < EDGE_THRESH;
    bool near_north = frac_lat > (1.0 - EDGE_THRESH);
    bool near_west  = frac_lon < EDGE_THRESH;
    bool near_east  = frac_lon > (1.0 - EDGE_THRESH);

    int adj_lat = near_south ? center.y - 1 : (near_north ? center.y + 1 : center.y);
    int adj_lon = near_west  ? center.x - 1 : (near_east  ? center.x + 1 : center.x);

    if (near_south || near_north) {
        if (adj_lat >= -90 && adj_lat <= 89)
            tiles.push_back({-1, center.x, adj_lat});
    }
    if (near_west || near_east) {
        int lx = adj_lon;
        if (lx < -180) lx += 360;
        if (lx >= 180) lx -= 360;
        tiles.push_back({-1, lx, center.y});
    }
    if ((near_south || near_north) && (near_west || near_east)) {
        int lx = adj_lon;
        if (lx < -180) lx += 360;
        if (lx >= 180) lx -= 360;
        if (adj_lat >= -90 && adj_lat <= 89)
            tiles.push_back({-1, lx, adj_lat});
    }

    return tiles;
}

std::vector<TileCoord> HgtProvider::tiles_in_bounds(const mesh3d_bounds_t& bounds, int /*zoom*/) const {
    int min_y = static_cast<int>(std::floor(bounds.min_lat));
    int max_y = static_cast<int>(std::floor(bounds.max_lat));
    int min_x = static_cast<int>(std::floor(bounds.min_lon));
    int max_x = static_cast<int>(std::floor(bounds.max_lon));

    std::vector<TileCoord> tiles;
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            tiles.push_back({-1, x, y});
        }
    }
    return tiles;
}

std::optional<TileData> HgtProvider::fetch_tile(const TileCoord& coord) {
    std::string filename = coord_to_filename(coord);

    auto raw = acquire_hgt(filename);
    if (raw.empty()) {
        LOG_WARN("HGT: no data for %s", filename.c_str());
        return std::nullopt;
    }

    int rows = 0, cols = 0;
    auto elevation = read_hgt(raw, rows, cols);
    if (elevation.empty()) {
        LOG_WARN("HGT: failed to parse %s", filename.c_str());
        return std::nullopt;
    }

    TileData td;
    td.coord = coord;
    td.bounds = hgt_tile_bounds(coord);
    td.elevation = std::move(elevation);
    td.elev_rows = rows;
    td.elev_cols = cols;

    LOG_INFO("HGT: loaded %s (%dx%d)", filename.c_str(), rows, cols);
    return td;
}

std::vector<float> HgtProvider::read_hgt(const std::vector<uint8_t>& data, int& rows, int& cols) {
    size_t samples = data.size() / 2; // int16 samples

    if (samples == 3601 * 3601) {
        rows = cols = 3601; // SRTM1
    } else if (samples == 1201 * 1201) {
        rows = cols = 1201; // SRTM3
    } else {
        LOG_WARN("HGT: unexpected size %zu bytes (%zu samples)", data.size(), samples);
        return {};
    }

    std::vector<float> elev(samples);
    const uint8_t* p = data.data();

    for (size_t i = 0; i < samples; ++i) {
        // Big-endian int16
        int16_t val = static_cast<int16_t>((p[i * 2] << 8) | p[i * 2 + 1]);
        // SRTM void = -32768 or very negative values
        if (val < -1000) val = 0;
        elev[i] = static_cast<float>(val);
    }

    return elev;
}

std::vector<uint8_t> HgtProvider::acquire_hgt(const std::string& filename) {
    // Check disk cache first
    if (m_cache.has(filename)) {
        LOG_DEBUG("HGT cache hit: %s", filename.c_str());
        return m_cache.read(filename);
    }

    // Download and decompress
    auto compressed = download_hgt(filename);
    if (compressed.empty()) return {};

    auto raw = decompress_gz(compressed);
    if (raw.empty()) {
        LOG_WARN("HGT: decompression failed for %s", filename.c_str());
        return {};
    }

    // Cache the uncompressed file
    m_cache.write(filename, raw);
    LOG_INFO("HGT: cached %s (%zu bytes)", filename.c_str(), raw.size());
    return raw;
}

/* libcurl write callback */
static size_t curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::vector<uint8_t>*>(userdata);
    size_t total = size * nmemb;
    buf->insert(buf->end(),
                static_cast<uint8_t*>(ptr),
                static_cast<uint8_t*>(ptr) + total);
    return total;
}

std::vector<uint8_t> HgtProvider::download_hgt(const std::string& filename) {
    // Build S3 URL: https://s3.amazonaws.com/elevation-tiles-prod/skadi/N38/N38W106.hgt.gz
    // Extract the lat directory from filename (first 3 chars, e.g. "N38")
    std::string lat_dir = filename.substr(0, 3);
    std::string url = "https://s3.amazonaws.com/elevation-tiles-prod/skadi/"
                    + lat_dir + "/" + filename + ".gz";

    LOG_INFO("HGT: downloading %s", url.c_str());

    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("HGT: curl_easy_init failed");
        return {};
    }

    std::vector<uint8_t> buffer;
    buffer.reserve(3 * 1024 * 1024); // ~3MB typical compressed

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_WARN("HGT: download failed: %s", curl_easy_strerror(res));
        return {};
    }
    if (http_code != 200) {
        LOG_WARN("HGT: HTTP %ld for %s", http_code, url.c_str());
        return {};
    }

    LOG_INFO("HGT: downloaded %zu bytes", buffer.size());
    return buffer;
}

std::vector<uint8_t> HgtProvider::decompress_gz(const std::vector<uint8_t>& compressed) {
    if (compressed.size() < 10) return {};

    z_stream strm{};
    // 15 + 16 = gzip decoding
    if (inflateInit2(&strm, 15 + 16) != Z_OK) {
        LOG_ERROR("HGT: inflateInit2 failed");
        return {};
    }

    strm.next_in = const_cast<uint8_t*>(compressed.data());
    strm.avail_in = static_cast<uInt>(compressed.size());

    // SRTM1: 3601*3601*2 = ~25MB, SRTM3: 1201*1201*2 = ~2.9MB
    std::vector<uint8_t> output;
    output.resize(30 * 1024 * 1024); // 30MB max

    strm.next_out = output.data();
    strm.avail_out = static_cast<uInt>(output.size());

    int ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        LOG_WARN("HGT: inflate failed (ret=%d)", ret);
        return {};
    }

    size_t decompressed_size = output.size() - strm.avail_out;
    output.resize(decompressed_size);
    return output;
}

} // namespace mesh3d
