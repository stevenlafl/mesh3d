#include "app.h"
#include "tile/hgt_provider.h"
#include "tile/url_tile_provider.h"
#include "util/math_util.h"
#include "util/log.h"
#include <glad/glad.h>
#include <cstring>
#include <filesystem>

namespace mesh3d {

static App g_app;
App& app() { return g_app; }

bool App::init(int width, int height, const char* title) {
    m_width = width;
    m_height = height;

    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    m_window = SDL_CreateWindow(
        title ? title : "mesh3d",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!m_window) {
        LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    m_gl_ctx = SDL_GL_CreateContext(m_window);
    if (!m_gl_ctx) {
        LOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        LOG_ERROR("GLAD failed to load GL");
        return false;
    }

    SDL_GL_SetSwapInterval(1); // vsync
    glEnable(GL_MULTISAMPLE);

    LOG_INFO("OpenGL %s, GLSL %s",
             glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));

    /* Find shader directory (look relative to executable or cwd) */
    namespace fs = std::filesystem;
    for (auto& candidate : {"shaders", "../shaders", "../../shaders"}) {
        if (fs::exists(candidate)) {
            m_shader_dir = candidate;
            break;
        }
    }
    if (m_shader_dir.empty()) {
        LOG_WARN("Shader directory not found, using 'shaders'");
        m_shader_dir = "shaders";
    }

    if (!renderer.init(m_shader_dir)) {
        LOG_ERROR("Renderer init failed");
        return false;
    }

    /* Default camera position */
    camera.position = glm::vec3(0, 500, 200);
    camera.rotate(0, 0); // force vector update

    LOG_INFO("mesh3d initialized (%dx%d)", width, height);
    return true;
}

void App::shutdown() {
    scene.clear();
    if (m_gl_ctx) { SDL_GL_DeleteContext(m_gl_ctx); m_gl_ctx = nullptr; }
    if (m_window) { SDL_DestroyWindow(m_window); m_window = nullptr; }
    SDL_Quit();
    LOG_INFO("mesh3d shut down");
}

bool App::init_hgt_mode(double center_lat, double center_lon) {
    LOG_INFO("Initializing HGT mode: center (%.4f, %.4f)", center_lat, center_lon);

    /* Initial bounds: center tile (camera-driven loading will expand as needed) */
    auto center_coord = HgtProvider::latlon_to_hgt_coord(center_lat, center_lon);
    mesh3d_bounds_t initial_bounds = HgtProvider::hgt_tile_bounds(center_coord);

    scene.bounds = initial_bounds;
    m_proj.init(initial_bounds);

    /* Create HGT provider and attach to tile manager */
    auto hgt = std::make_unique<HgtProvider>();
    scene.tile_manager.set_hgt_provider(std::move(hgt));
    scene.tile_manager.set_bounds(initial_bounds);

    scene.use_tile_system = true;
    m_hgt_mode = true;

    /* Position camera above center */
    auto lc = m_proj.project(center_lat, center_lon);
    camera.position = glm::vec3(lc.x, 2000.0f, lc.z);
    camera.pitch = -30.0f;
    camera.rotate(0, 0);

    LOG_INFO("HGT mode ready, camera at (%.0f, %.0f, %.0f)",
             camera.position.x, camera.position.y, camera.position.z);
    return true;
}

bool App::set_terrain(const mesh3d_grid_f32_t& grid, const mesh3d_bounds_t& bounds) {
    scene.bounds = bounds;
    scene.grid_rows = grid.rows;
    scene.grid_cols = grid.cols;
    scene.elevation.assign(grid.data, grid.data + grid.rows * grid.cols);
    scene.build_terrain();
    scene.build_flat_plane();
    return true;
}

int App::add_node(const mesh3d_node_t& node) {
    GeoProjection proj;
    proj.init(scene.bounds);
    NodeData nd;
    nd.info = node;
    auto lc = proj.project(node.lat, node.lon);
    nd.world_pos = glm::vec3(lc.x, static_cast<float>(node.alt + node.antenna_height_m), lc.z);
    scene.nodes.push_back(nd);
    return static_cast<int>(scene.nodes.size() - 1);
}

bool App::set_viewshed(int node_idx, const mesh3d_grid_u8_t& vis, const mesh3d_grid_f32_t& signal) {
    (void)node_idx; // individual viewsheds stored for future per-node display
    /* For now, use as merged if no merged data */
    if (scene.viewshed_vis.empty() && vis.data) {
        scene.viewshed_vis.assign(vis.data, vis.data + vis.rows * vis.cols);
    }
    if (scene.signal_strength.empty() && signal.data) {
        scene.signal_strength.assign(signal.data, signal.data + signal.rows * signal.cols);
    }
    return true;
}

bool App::set_merged_coverage(const mesh3d_grid_u8_t& vis, const mesh3d_grid_u8_t& overlap) {
    if (vis.data) {
        scene.viewshed_vis.assign(vis.data, vis.data + vis.rows * vis.cols);
    }
    if (overlap.data) {
        scene.overlap_count.assign(overlap.data, overlap.data + overlap.rows * overlap.cols);
    }
    return true;
}

void App::set_render_mode(mesh3d_render_mode_t mode) {
    scene.render_mode = mode;
    LOG_INFO("Render mode: %s", mode == MESH3D_MODE_TERRAIN ? "Terrain" : "Flat");
}

void App::set_overlay_mode(mesh3d_overlay_mode_t mode) {
    scene.overlay_mode = mode;
    LOG_INFO("Overlay: %s",
             mode == MESH3D_OVERLAY_NONE ? "none" :
             mode == MESH3D_OVERLAY_VIEWSHED ? "viewshed" : "signal");
}

void App::toggle_signal_spheres() {
    scene.show_signal_spheres = !scene.show_signal_spheres;
    LOG_INFO("Signal spheres: %s", scene.show_signal_spheres ? "ON" : "OFF");
}

void App::toggle_wireframe() {
    renderer.set_wireframe(!renderer.wireframe());
    LOG_INFO("Wireframe: %s", renderer.wireframe() ? "ON" : "OFF");
}

void App::cycle_imagery_source() {
    scene.tile_manager.cycle_imagery_source();
}

void App::rebuild_scene() {
    scene.rebuild_all();
}

void App::handle_toggles() {
    if (m_input.consume_tab()) {
        set_render_mode(scene.render_mode == MESH3D_MODE_TERRAIN
                        ? MESH3D_MODE_FLAT : MESH3D_MODE_TERRAIN);
    }
    if (m_input.consume_key1()) set_overlay_mode(MESH3D_OVERLAY_VIEWSHED);
    if (m_input.consume_key2()) set_overlay_mode(MESH3D_OVERLAY_SIGNAL);
    if (m_input.consume_key3()) cycle_imagery_source();
    if (m_input.consume_keyT()) toggle_signal_spheres();
    if (m_input.consume_keyF()) toggle_wireframe();
}

bool App::poll_events() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        m_input.process_event(ev, camera);
        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            m_width = ev.window.data1;
            m_height = ev.window.data2;
            glViewport(0, 0, m_width, m_height);
        }
    }
    return !m_input.quit_requested();
}

void App::frame(float dt) {
    handle_toggles();
    m_input.update(camera, dt);

    /* Update tile system */
    if (scene.use_tile_system) {
        if (m_hgt_mode) {
            scene.tile_manager.update(camera, m_proj);
        } else {
            scene.tile_manager.update();
        }
    }

    float aspect = static_cast<float>(m_width) / std::max(m_height, 1);
    renderer.render(scene, camera, aspect);

    SDL_GL_SwapWindow(m_window);
}

void App::run() {
    Uint64 last = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();

    while (poll_events()) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - last) / freq;
        last = now;
        dt = std::min(dt, 0.1f); // cap delta

        frame(dt);
    }
}

} // namespace mesh3d
