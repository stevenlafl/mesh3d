#include <mesh3d/mesh3d.h>
#include "app.h"

using namespace mesh3d;

int mesh3d_init(int w, int h, const char* title) {
    return app().init(w, h, title) ? 1 : 0;
}

void mesh3d_shutdown(void) {
    app().shutdown();
}

/* DB functions are stubs for ABI compatibility */
int mesh3d_connect_db(const char* /*conninfo*/) {
    return 0;
}

int mesh3d_load_project(int /*project_id*/) {
    return 0;
}

void mesh3d_disconnect_db(void) {
}

int mesh3d_set_terrain(mesh3d_grid_f32_t grid, mesh3d_bounds_t bounds) {
    return app().set_terrain(grid, bounds) ? 1 : 0;
}

int mesh3d_add_node(mesh3d_node_t node) {
    return app().add_node(node);
}

int mesh3d_set_viewshed(int node_idx, mesh3d_grid_u8_t vis, mesh3d_grid_f32_t signal) {
    return app().set_viewshed(node_idx, vis, signal) ? 1 : 0;
}

int mesh3d_set_merged_coverage(mesh3d_grid_u8_t vis, mesh3d_grid_u8_t overlap) {
    return app().set_merged_coverage(vis, overlap) ? 1 : 0;
}

void mesh3d_set_render_mode(mesh3d_render_mode_t mode) {
    app().set_render_mode(mode);
}

void mesh3d_set_overlay_mode(mesh3d_overlay_mode_t mode) {
    app().set_overlay_mode(mode);
}

void mesh3d_toggle_signal_spheres(void) {
    app().toggle_signal_spheres();
}

void mesh3d_toggle_wireframe(void) {
    app().toggle_wireframe();
}

void mesh3d_rebuild_scene(void) {
    app().rebuild_scene();
}

void mesh3d_run(void) {
    app().run();
}

int mesh3d_poll_events(void) {
    return app().poll_events() ? 1 : 0;
}

void mesh3d_frame(float dt) {
    app().frame(dt);
}
