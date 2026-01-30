#pragma once
#include "tile/tile_coord.h"
#include "render/mesh.h"
#include "render/texture.h"
#include <mesh3d/types.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace mesh3d {

/* CPU-side raw tile data (elevation, imagery, overlays) */
struct TileData {
    TileCoord coord;
    mesh3d_bounds_t bounds;

    /* Elevation grid */
    std::vector<float> elevation;
    int elev_rows = 0, elev_cols = 0;

    /* Imagery (RGBA) */
    std::vector<uint8_t> imagery;
    int img_width = 0, img_height = 0;

    /* Overlay data */
    std::vector<uint8_t> viewshed;
    std::vector<float> signal;
};

/* GPU-side tile ready for rendering.
   Also retains CPU-side elevation for runtime queries (raycast, etc.) */
struct TileRenderable {
    TileCoord coord;
    mesh3d_bounds_t bounds;
    Mesh mesh;
    Texture texture;
    glm::mat4 model{1.0f};

    /* CPU-side elevation retained for sampling */
    std::vector<float> elevation;
    int elev_rows = 0, elev_cols = 0;

    /* CPU-side overlay data (populated by viewshed computation) */
    std::vector<uint8_t> viewshed;
    std::vector<float> signal;
};

} // namespace mesh3d
