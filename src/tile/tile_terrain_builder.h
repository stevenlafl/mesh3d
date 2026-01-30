#pragma once
#include "tile/tile_data.h"

namespace mesh3d {

struct GeoProjection;

/* Converts TileData (CPU) -> TileRenderable (GPU).
   Uses build_terrain_mesh() for geometry, uploads imagery as Texture. */
class TileTerrainBuilder {
public:
    float elevation_scale = 1.0f;

    /* Build a renderable tile from raw data.
       If tile has elevation, builds terrain mesh.
       If tile has imagery, uploads as texture. */
    TileRenderable build(const TileData& data, const GeoProjection& proj) const;

    /* Build mesh-only from elevation data (no imagery) */
    Mesh build_mesh(const TileData& data, const GeoProjection& proj) const;

    /* Upload imagery data as texture */
    Texture build_texture(const TileData& data) const;
};

} // namespace mesh3d
