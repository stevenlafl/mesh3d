#include "tile/single_tile_provider.h"

namespace mesh3d {

void SingleTileProvider::set_data(const mesh3d_bounds_t& bounds,
                                   const float* elevation, int rows, int cols,
                                   const uint8_t* viewshed,
                                   const float* signal) {
    m_bounds = bounds;
    m_rows = rows;
    m_cols = cols;

    int n = rows * cols;
    if (elevation && n > 0) {
        m_elevation.assign(elevation, elevation + n);
    } else {
        m_elevation.clear();
    }

    if (viewshed && n > 0) {
        m_viewshed.assign(viewshed, viewshed + n);
    } else {
        m_viewshed.clear();
    }

    if (signal && n > 0) {
        m_signal.assign(signal, signal + n);
    } else {
        m_signal.clear();
    }

    m_has_data = !m_elevation.empty() && m_rows >= 2 && m_cols >= 2;
}

std::optional<TileData> SingleTileProvider::fetch_tile(const TileCoord& coord) {
    if (!m_has_data) return std::nullopt;
    /* Only tile {0,0,0} exists */
    if (coord.z != 0 || coord.x != 0 || coord.y != 0) return std::nullopt;

    TileData td;
    td.coord = {0, 0, 0};
    td.bounds = m_bounds;
    td.elevation = m_elevation;
    td.elev_rows = m_rows;
    td.elev_cols = m_cols;
    td.viewshed = m_viewshed;
    td.signal = m_signal;
    return td;
}

std::vector<TileCoord> SingleTileProvider::tiles_in_bounds(const mesh3d_bounds_t&, int) const {
    if (!m_has_data) return {};
    return {{0, 0, 0}};
}

} // namespace mesh3d
