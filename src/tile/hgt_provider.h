#pragma once
#include "tile/tile_provider.h"
#include "tile/disk_cache.h"
#include <vector>
#include <string>
#include <cstdint>

namespace mesh3d {

/* Provides elevation data from SRTM HGT files.
   Downloads .hgt.gz from AWS S3 on demand, caches to ~/.cache/mesh3d/hgt/.
   Each tile is 1 degree x 1 degree (SRTM1: 3601x3601, SRTM3: 1201x1201).
   TileCoord scheme: z=-1 (sentinel), x=floor(lon), y=floor(lat). */
class HgtProvider : public TileProvider {
public:
    HgtProvider();

    const char* name() const override { return "hgt"; }
    mesh3d_bounds_t coverage() const override;
    int min_zoom() const override { return 0; }
    int max_zoom() const override { return 0; }

    std::optional<TileData> fetch_tile(const TileCoord& coord) override;
    std::vector<TileCoord> tiles_in_bounds(const mesh3d_bounds_t& bounds, int zoom) const override;

    /* Get the 1-4 tiles the camera straddles (based on proximity to tile edges) */
    std::vector<TileCoord> tiles_in_view(double lat, double lon) const;

    /* Convert lat/lon to HGT TileCoord (z=-1, x=floor(lon), y=floor(lat)) */
    static TileCoord latlon_to_hgt_coord(double lat, double lon);
    /* Convert HGT TileCoord to filename (e.g. "N38W106.hgt") */
    static std::string coord_to_filename(const TileCoord& coord);
    /* Get geographic bounds of an HGT tile */
    static mesh3d_bounds_t hgt_tile_bounds(const TileCoord& coord);

private:
    DiskCache m_cache;

    std::vector<float> read_hgt(const std::vector<uint8_t>& data, int& rows, int& cols);
    std::vector<uint8_t> acquire_hgt(const std::string& filename);
    std::vector<uint8_t> download_hgt(const std::string& filename);
    static std::vector<uint8_t> decompress_gz(const std::vector<uint8_t>& compressed);
};

} // namespace mesh3d
