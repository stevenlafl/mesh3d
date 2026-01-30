#include "tile/tile_selector.h"

namespace mesh3d {

std::vector<TileCoord> TileSelector::select(const mesh3d_bounds_t& bounds) const {
    return bounds_to_tile_range(bounds, fixed_zoom);
}

} // namespace mesh3d
