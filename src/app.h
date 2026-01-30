#pragma once
#include <SDL2/SDL.h>
#include <string>
#include "scene/scene.h"
#include "render/renderer.h"
#include "camera/camera.h"
#include "camera/input.h"
#include "db/db.h"
#include <mesh3d/types.h>

namespace mesh3d {

class App {
public:
    /* Initialize SDL2 window + GL context */
    bool init(int width, int height, const char* title);
    void shutdown();

    /* Database */
    bool connect_db(const char* conninfo);
    bool load_project(int project_id);
    void disconnect_db();

    /* Direct data injection */
    bool set_terrain(const mesh3d_grid_f32_t& grid, const mesh3d_bounds_t& bounds);
    int  add_node(const mesh3d_node_t& node);
    bool set_viewshed(int node_idx, const mesh3d_grid_u8_t& vis, const mesh3d_grid_f32_t& signal);
    bool set_merged_coverage(const mesh3d_grid_u8_t& vis, const mesh3d_grid_u8_t& overlap);

    /* Control */
    void set_render_mode(mesh3d_render_mode_t mode);
    void set_overlay_mode(mesh3d_overlay_mode_t mode);
    void toggle_signal_spheres();
    void toggle_wireframe();
    void rebuild_scene();
    void cycle_imagery_source();

    /* Main loop */
    void run();
    bool poll_events(); // returns false on quit
    void frame(float dt);

    Scene    scene;
    Camera   camera;
    Renderer renderer;

private:
    SDL_Window*   m_window  = nullptr;
    SDL_GLContext  m_gl_ctx  = nullptr;
    int m_width = 1280, m_height = 720;

    InputHandler m_input;
    Database     m_db;
    std::string  m_shader_dir;

    void handle_toggles();

    /* Generate synthetic demo data */
    void generate_demo_scene();
};

/* Global app instance */
App& app();

} // namespace mesh3d
