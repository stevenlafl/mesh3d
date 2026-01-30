#pragma once
#include "tile/tile_provider.h"
#include "tile/disk_cache.h"
#include <string>
#include <memory>

namespace mesh3d {

/* Fetches imagery tiles from a URL template (slippy map {z}/{x}/{y}).
   Downloads via libcurl, decodes with stb_image, caches to disk. */
class UrlTileProvider : public TileProvider {
public:
    UrlTileProvider(const std::string& name,
                    const std::string& url_template,
                    const std::string& file_ext,
                    int min_zoom, int max_zoom,
                    const std::string& user_agent = "");

    const char* name() const override { return m_name.c_str(); }
    mesh3d_bounds_t coverage() const override;
    int min_zoom() const override { return m_min_zoom; }
    int max_zoom() const override { return m_max_zoom; }

    std::optional<TileData> fetch_tile(const TileCoord& coord) override;

    /* Predefined source factories */
    static std::unique_ptr<UrlTileProvider> satellite();
    static std::unique_ptr<UrlTileProvider> street();

private:
    std::string m_name;
    std::string m_url_template; // e.g. "https://.../{z}/{y}/{x}"
    std::string m_file_ext;     // e.g. "jpg" or "png"
    int m_min_zoom, m_max_zoom;
    std::string m_user_agent;
    DiskCache m_cache;

    std::string build_url(const TileCoord& coord) const;
    std::string cache_key(const TileCoord& coord) const;

    /* Download raw bytes from URL. Returns empty on failure. */
    std::vector<uint8_t> download(const std::string& url) const;
};

} // namespace mesh3d
