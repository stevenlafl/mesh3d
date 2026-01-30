#pragma once
#include "render/mesh.h"
#include <mesh3d/types.h>
#include <vector>

namespace mesh3d {

struct GeoProjection;

/* Build a terrain mesh from elevation grid data.
   Vertex layout per vertex:
     float3 position (X, Y, Z)
     float3 normal
     float2 uv
     float  viewshed_flag (0 or 1)
     float  signal_dbm
   Total: 11 floats per vertex.
*/

struct TerrainBuildData {
    const float*   elevation;    // row-major, rows x cols
    int rows, cols;
    mesh3d_bounds_t bounds;
    float elevation_scale;       // Y multiplier (1.0 = real meters)

    /* Optional overlay data (may be null) */
    const uint8_t* viewshed;    // rows x cols
    const float*   signal;      // rows x cols (dBm)
};

Mesh build_terrain_mesh(const TerrainBuildData& data, const GeoProjection& proj);
Mesh build_flat_mesh(int rows, int cols, float width_m, float height_m);

/* Generate synthetic terrain for testing */
void generate_synthetic_terrain(std::vector<float>& out, int rows, int cols);

} // namespace mesh3d
