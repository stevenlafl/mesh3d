#pragma once
#include "tile/tile_provider.h"
#include <vector>
#include <cstdint>

namespace mesh3d {

/* Wraps existing scene elevation + viewshed/signal as a single tile at {0,0,0}. */
class SingleTileProvider : public TileProvider {
public:
    const char* name() const override { return "single"; }
    mesh3d_bounds_t coverage() const override { return m_bounds; }
    int min_zoom() const override { return 0; }
    int max_zoom() const override { return 0; }

    std::optional<TileData> fetch_tile(const TileCoord& coord) override;
    std::vector<TileCoord> tiles_in_bounds(const mesh3d_bounds_t& bounds, int zoom) const override;

    /* Set source data from scene arrays */
    void set_data(const mesh3d_bounds_t& bounds,
                  const float* elevation, int rows, int cols,
                  const uint8_t* viewshed,
                  const float* signal);

private:
    mesh3d_bounds_t m_bounds{};
    std::vector<float> m_elevation;
    int m_rows = 0, m_cols = 0;
    std::vector<uint8_t> m_viewshed;
    std::vector<float> m_signal;
    bool m_has_data = false;
};

} // namespace mesh3d
