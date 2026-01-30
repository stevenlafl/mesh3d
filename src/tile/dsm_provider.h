#pragma once
#include "tile/tile_provider.h"
#include "tile/disk_cache.h"
#include <vector>
#include <string>
#include <cstdint>

namespace mesh3d {

/* Provides high-resolution (1-2m) LiDAR Digital Surface Model tiles.
   Reads GeoTIFF files from a local directory.
   TileCoord scheme: z=-2 (sentinel for DSM), x=floor(lon*100), y=floor(lat*100).
   Each tile covers a small area (depends on source data). */
class DSMProvider : public TileProvider {
public:
    DSMProvider();

    const char* name() const override { return "dsm"; }
    mesh3d_bounds_t coverage() const override;
    int min_zoom() const override { return 0; }
    int max_zoom() const override { return 0; }

    std::optional<TileData> fetch_tile(const TileCoord& coord) override;
    std::vector<TileCoord> tiles_in_bounds(const mesh3d_bounds_t& bounds, int zoom) const override;

    /* Get tiles the camera needs based on lat/lon */
    std::vector<TileCoord> tiles_in_view(double lat, double lon) const;

    /* Set local directory containing GeoTIFF tiles */
    void set_data_dir(const std::string& dir);

    /* Set URL template for dynamic download (optional).
       Placeholders: {lat}, {lon}, {n}, {s}, {e}, {w} */
    void set_url_template(const std::string& tmpl);

    /* Convert lat/lon to DSM TileCoord */
    static TileCoord latlon_to_dsm_coord(double lat, double lon);
    /* Get geographic bounds of a DSM tile */
    static mesh3d_bounds_t dsm_tile_bounds(const TileCoord& coord);

private:
    std::string m_data_dir;
    std::string m_url_template;
    DiskCache m_cache;

    /* Scan directory for GeoTIFF files and index their bounds */
    void scan_directory();
    bool m_scanned = false;

    struct TileIndex {
        std::string filepath;
        mesh3d_bounds_t bounds;
        TileCoord coord;
    };
    std::vector<TileIndex> m_index;

    std::optional<TileData> load_geotiff(const std::string& path);
};

} // namespace mesh3d
