#include "tile/tile_terrain_builder.h"
#include "scene/terrain.h"
#include "util/math_util.h"
#include "util/log.h"

namespace mesh3d {

TileRenderable TileTerrainBuilder::build(const TileData& data, const GeoProjection& proj) const {
    TileRenderable tr;
    tr.coord = data.coord;
    tr.bounds = data.bounds;
    tr.model = glm::mat4(1.0f);

    if (data.elev_rows >= 2 && data.elev_cols >= 2 && !data.elevation.empty()) {
        tr.mesh = build_mesh(data, proj);
        /* Retain CPU-side elevation for runtime queries */
        tr.elevation = data.elevation;
        tr.elev_rows = data.elev_rows;
        tr.elev_cols = data.elev_cols;
    }

    if (!data.imagery.empty() && data.img_width > 0 && data.img_height > 0) {
        tr.texture = build_texture(data);
    }

    return tr;
}

Mesh TileTerrainBuilder::build_mesh(const TileData& data, const GeoProjection& proj) const {
    TerrainBuildData td;
    td.elevation = data.elevation.data();
    td.rows = data.elev_rows;
    td.cols = data.elev_cols;
    td.bounds = data.bounds;
    td.elevation_scale = elevation_scale;
    /* Viewshed/signal use overlay textures in tile mode — don't bake into vertices */
    td.viewshed = nullptr;
    td.signal = nullptr;

    return build_terrain_mesh(td, proj);
}

Mesh TileTerrainBuilder::rebuild_mesh(const TileRenderable& tr, const GeoProjection& proj) const {
    TerrainBuildData td;
    td.elevation = tr.elevation.data();
    td.rows = tr.elev_rows;
    td.cols = tr.elev_cols;
    td.bounds = tr.bounds;
    td.elevation_scale = elevation_scale;
    /* Viewshed/signal use overlay textures in tile mode — don't bake into vertices */
    td.viewshed = nullptr;
    td.signal = nullptr;

    return build_terrain_mesh(td, proj);
}

Texture TileTerrainBuilder::build_texture(const TileData& data) const {
    Texture tex;
    tex.load_rgba(data.imagery.data(), data.img_width, data.img_height);
    return tex;
}

} // namespace mesh3d
