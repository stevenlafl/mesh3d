#include "tile/url_tile_provider.h"
#include "util/log.h"
#include <curl/curl.h>
#include <stb_image.h>
#include <algorithm>
#include <sstream>

namespace mesh3d {

UrlTileProvider::UrlTileProvider(const std::string& name,
                                 const std::string& url_template,
                                 const std::string& file_ext,
                                 int min_zoom, int max_zoom,
                                 const std::string& user_agent)
    : m_name(name)
    , m_url_template(url_template)
    , m_file_ext(file_ext)
    , m_min_zoom(min_zoom)
    , m_max_zoom(max_zoom)
    , m_user_agent(user_agent)
{}

mesh3d_bounds_t UrlTileProvider::coverage() const {
    /* Global coverage */
    return {-85.05, 85.05, -180.0, 180.0};
}

std::string UrlTileProvider::build_url(const TileCoord& coord) const {
    std::string url = m_url_template;
    auto replace = [&](const std::string& token, int value) {
        size_t pos = url.find(token);
        if (pos != std::string::npos) {
            url.replace(pos, token.size(), std::to_string(value));
        }
    };
    replace("{z}", coord.z);
    replace("{x}", coord.x);
    replace("{y}", coord.y);
    return url;
}

std::string UrlTileProvider::cache_key(const TileCoord& coord) const {
    std::ostringstream ss;
    ss << m_name << "/" << coord.z << "/" << coord.x << "/" << coord.y << "." << m_file_ext;
    return ss.str();
}

/* libcurl write callback */
static size_t curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::vector<uint8_t>*>(userdata);
    size_t total = size * nmemb;
    auto* bytes = static_cast<uint8_t*>(ptr);
    buf->insert(buf->end(), bytes, bytes + total);
    return total;
}

std::vector<uint8_t> UrlTileProvider::download(const std::string& url) const {
    std::vector<uint8_t> data;
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("curl_easy_init failed");
        return data;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    if (!m_user_agent.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, m_user_agent.c_str());
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        LOG_WARN("Download failed: %s -> %s", url.c_str(), curl_easy_strerror(res));
        data.clear();
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 200) {
            LOG_WARN("HTTP %ld for %s", http_code, url.c_str());
            data.clear();
        }
    }

    curl_easy_cleanup(curl);
    return data;
}

std::optional<TileData> UrlTileProvider::fetch_tile(const TileCoord& coord) {
    std::string key = cache_key(coord);

    /* Try disk cache first */
    std::vector<uint8_t> raw;
    if (m_cache.has(key)) {
        raw = m_cache.read(key);
        LOG_DEBUG("Cache hit: %s", key.c_str());
    }

    /* Download if not cached */
    if (raw.empty()) {
        std::string url = build_url(coord);
        LOG_INFO("Downloading tile %s", url.c_str());
        raw = download(url);
        if (raw.empty()) return std::nullopt;

        /* Cache to disk */
        m_cache.write(key, raw);
    }

    /* Decode image with stb_image */
    int w, h, ch;
    stbi_set_flip_vertically_on_load(false); // tiles are top-left origin
    unsigned char* pixels = stbi_load_from_memory(raw.data(), static_cast<int>(raw.size()),
                                                   &w, &h, &ch, 4); // force RGBA
    if (!pixels) {
        LOG_WARN("Failed to decode tile image: %s", key.c_str());
        return std::nullopt;
    }

    TileData td;
    td.coord = coord;
    td.bounds = tile_bounds(coord);
    td.imagery.assign(pixels, pixels + w * h * 4);
    td.img_width = w;
    td.img_height = h;

    stbi_image_free(pixels);
    return td;
}

std::unique_ptr<UrlTileProvider> UrlTileProvider::satellite() {
    return std::make_unique<UrlTileProvider>(
        "esri_satellite",
        "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
        "jpg",
        0, 18);
}

std::unique_ptr<UrlTileProvider> UrlTileProvider::street() {
    return std::make_unique<UrlTileProvider>(
        "osm",
        "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
        "png",
        0, 19,
        "mesh3d/0.1 (tile viewer)");
}

} // namespace mesh3d
