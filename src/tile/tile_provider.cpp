#include "tile/tile_provider.h"

namespace mesh3d {

std::vector<TileCoord> TileProvider::tiles_in_bounds(const mesh3d_bounds_t& bounds, int zoom) const {
    return bounds_to_tile_range(bounds, zoom);
}

} // namespace mesh3d
