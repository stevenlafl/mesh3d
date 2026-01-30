#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <optional>
#include "scene/scene.h"
#include "render/renderer.h"
#include "camera/camera.h"
#include "camera/input.h"
#include "ui/hud.h"
#include "analysis/gpu_viewshed.h"
#include "util/math_util.h"
#include <mesh3d/types.h>

namespace mesh3d {

class App {
public:
    /* Initialize SDL2 window + GL context */
    bool init(int width, int height, const char* title);
    void shutdown();

    /* HGT streaming mode (no DB needed) */
    bool init_hgt_mode(double center_lat, double center_lon);

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
    void set_propagation_model(mesh3d_prop_model_t model);
    void set_itm_params(const mesh3d_itm_params_t& params);
    void set_dsm_dir(const std::string& dir);

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
    Hud          m_hud;
    std::string  m_shader_dir;
    GeoProjection m_proj;
    bool m_hgt_mode = false;
    bool m_has_compute = false;
    GpuViewshed m_gpu_viewshed;
    bool m_viewshed_pending = false;

    /* HUD state */
    bool m_show_controls = true;
    bool m_node_placement_mode = false;

    void handle_toggles();
    void handle_menu_input();
    void handle_node_placement();

    /* Terrain raycast from camera center */
    std::optional<glm::vec3> raycast_terrain();

    /* Place a node at a world position */
    void place_node_at(const glm::vec3& world_pos);

    /* Delete nearest node to a world position */
    void delete_nearest_node(const glm::vec3& world_pos);

    /* Find font path (search relative to exe) */
    std::string find_font_path();
};

/* Global app instance */
App& app();

} // namespace mesh3d
