#include "scene/scene.h"
#include "scene/terrain.h"
#include "scene/node_marker.h"
#include "scene/signal_sphere.h"
#include "tile/single_tile_provider.h"
#include "tile/url_tile_provider.h"
#include "util/math_util.h"
#include "util/color.h"
#include "util/log.h"
#include <glm/gtc/matrix_transform.hpp>

namespace mesh3d {

void Scene::clear() {
    terrain_mesh = Mesh();
    flat_mesh = Mesh();
    marker_meshes.clear();
    marker_models.clear();
    marker_colors.clear();
    sphere_meshes.clear();
    sphere_models.clear();
    sphere_colors.clear();
    sphere_centers.clear();
    nodes.clear();
    elevation.clear();
    viewshed_vis.clear();
    signal_strength.clear();
    overlap_count.clear();
    grid_rows = grid_cols = 0;
    tile_manager.clear();
    use_tile_system = false;
}

void Scene::build_terrain(float elev_scale) {
    if (elevation.empty() || grid_rows < 2 || grid_cols < 2) {
        LOG_WARN("No elevation data to build terrain");
        return;
    }

    GeoProjection proj;
    proj.init(bounds);

    TerrainBuildData td;
    td.elevation = elevation.data();
    td.rows = grid_rows;
    td.cols = grid_cols;
    td.bounds = bounds;
    td.elevation_scale = elev_scale;
    td.viewshed = viewshed_vis.empty() ? nullptr : viewshed_vis.data();
    td.signal   = signal_strength.empty() ? nullptr : signal_strength.data();

    terrain_mesh = build_terrain_mesh(td, proj);
    terrain_model = glm::mat4(1.0f);

    LOG_INFO("Built terrain mesh: %dx%d, %d triangles",
             grid_rows, grid_cols, terrain_mesh.element_count() / 3);
}

void Scene::build_flat_plane() {
    GeoProjection proj;
    proj.init(bounds);
    float w = proj.width_m(bounds);
    float h = proj.height_m(bounds);
    int r = grid_rows > 0 ? grid_rows : 100;
    int c = grid_cols > 0 ? grid_cols : 100;
    if (w < 1.0f) w = 10000.0f; // default 10km
    if (h < 1.0f) h = 10000.0f;

    flat_mesh = build_flat_mesh(r, c, w, h);
    flat_model = glm::mat4(1.0f);
}

void Scene::build_markers() {
    marker_meshes.clear();
    marker_models.clear();
    marker_colors.clear();

    if (nodes.empty()) return;

    Mesh proto = build_icosphere(1); // 42 verts, shared geometry

    for (auto& nd : nodes) {
        /* We clone geometry per marker — for few nodes this is fine. */
        marker_meshes.push_back(build_icosphere(1));
        float marker_radius = 15.0f; // 15m sphere
        glm::mat4 model = glm::translate(glm::mat4(1.0f), nd.world_pos);
        model = glm::scale(model, glm::vec3(marker_radius));
        marker_models.push_back(model);
        marker_colors.push_back(role_color(nd.info.role));
    }

    LOG_INFO("Built %zu node markers", nodes.size());
}

void Scene::build_spheres() {
    sphere_meshes.clear();
    sphere_models.clear();
    sphere_colors.clear();
    sphere_centers.clear();

    if (nodes.empty()) return;

    for (auto& nd : nodes) {
        sphere_meshes.push_back(build_signal_sphere());
        float radius = nd.info.max_range_km * 1000.0f; // km -> m
        if (radius < 100.0f) radius = 5000.0f; // default 5km
        glm::mat4 model = glm::translate(glm::mat4(1.0f), nd.world_pos);
        model = glm::scale(model, glm::vec3(radius));
        sphere_models.push_back(model);
        sphere_colors.push_back(role_color(nd.info.role));
        sphere_centers.push_back(nd.world_pos);
    }

    LOG_INFO("Built %zu signal spheres", nodes.size());
}

void Scene::rebuild_all() {
    build_terrain();
    build_flat_plane();
    build_markers();
    build_spheres();
    init_tile_provider();
}

void Scene::init_tile_provider() {
    if (elevation.empty() || grid_rows < 2 || grid_cols < 2) {
        use_tile_system = false;
        return;
    }

    /* Set up elevation provider from scene data */
    auto elev = std::make_unique<SingleTileProvider>();
    elev->set_data(bounds,
                   elevation.data(), grid_rows, grid_cols,
                   viewshed_vis.empty() ? nullptr : viewshed_vis.data(),
                   signal_strength.empty() ? nullptr : signal_strength.data());

    tile_manager.set_elevation_provider(std::move(elev));
    tile_manager.set_bounds(bounds);

    /* Set up default imagery provider */
    tile_manager.set_imagery_provider(UrlTileProvider::satellite());

    use_tile_system = true;
    LOG_INFO("Tile system initialized");
}

} // namespace mesh3d
