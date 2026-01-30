#pragma once
#include "tile/tile_coord.h"
#include <mesh3d/types.h>
#include <vector>

namespace mesh3d {

class Camera;

/* Selects which tiles to load/display for the current view.
   Phase 1: returns all tiles at a fixed zoom for the project bounds.
   Future: camera-based LOD with quadtree refinement. */
class TileSelector {
public:
    int fixed_zoom = 13; // default zoom for imagery tiles

    /* Select tiles for the given bounds.
       For the elevation provider (single tile), always returns {0,0,0}.
       For imagery, returns all tiles covering bounds at fixed_zoom. */
    std::vector<TileCoord> select(const mesh3d_bounds_t& bounds) const;
};

} // namespace mesh3d
