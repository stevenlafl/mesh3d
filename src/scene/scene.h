#pragma once
#include "render/mesh.h"
#include "render/texture.h"
#include "tile/tile_manager.h"
#include <mesh3d/types.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace mesh3d {

struct NodeData {
    mesh3d_node_t info;
    glm::vec3 world_pos;
};

struct Scene {
    /* Mode */
    mesh3d_render_mode_t  render_mode  = MESH3D_MODE_TERRAIN;
    mesh3d_overlay_mode_t overlay_mode = MESH3D_OVERLAY_NONE;
    bool show_signal_spheres = true;

    /* Terrain */
    Mesh      terrain_mesh;
    glm::mat4 terrain_model{1.0f};
    Texture   satellite_tex;

    /* Flat plane */
    Mesh      flat_mesh;
    glm::mat4 flat_model{1.0f};

    /* Node markers */
    std::vector<Mesh>      marker_meshes;
    std::vector<glm::mat4> marker_models;
    std::vector<glm::vec3> marker_colors;

    /* Signal spheres */
    std::vector<Mesh>      sphere_meshes;
    std::vector<glm::mat4> sphere_models;
    std::vector<glm::vec3> sphere_colors;
    std::vector<glm::vec3> sphere_centers;

    /* Source data */
    std::vector<NodeData>  nodes;
    mesh3d_bounds_t        bounds{};

    /* Elevation grid (kept for rebuilds) */
    std::vector<float>   elevation;
    int grid_rows = 0, grid_cols = 0;

    /* Viewshed/signal overlay data */
    std::vector<uint8_t> viewshed_vis;   // merged visibility
    std::vector<float>   signal_strength; // merged signal (dBm)
    std::vector<uint8_t> overlap_count;

    /* Receiver / display config */
    mesh3d_rf_config_t rf_config{-130.0f, 1.0f, 2.0f, 2.0f, -130.0f, -80.0f};

    /* Tile system */
    TileManager tile_manager;
    bool use_tile_system = false;

    void clear();
    void build_terrain(float elev_scale = 1.0f);
    void build_flat_plane();
    void build_markers();
    void build_spheres();
    void rebuild_all();
    void init_tile_provider();
};

} // namespace mesh3d
