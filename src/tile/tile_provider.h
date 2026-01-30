#pragma once
#include "tile/tile_coord.h"
#include "tile/tile_data.h"
#include <mesh3d/types.h>
#include <optional>
#include <vector>

namespace mesh3d {

class TileProvider {
public:
    virtual ~TileProvider() = default;

    virtual const char* name() const = 0;
    virtual mesh3d_bounds_t coverage() const = 0;
    virtual int min_zoom() const = 0;
    virtual int max_zoom() const = 0;

    /* Fetch tile data. Returns nullopt if tile not available. */
    virtual std::optional<TileData> fetch_tile(const TileCoord& coord) = 0;

    /* Get all tile coordinates covering bounds at given zoom.
       Default implementation uses bounds_to_tile_range(). */
    virtual std::vector<TileCoord> tiles_in_bounds(const mesh3d_bounds_t& bounds, int zoom) const;
};

} // namespace mesh3d
