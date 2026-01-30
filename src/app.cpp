#include "app.h"
#include "tile/hgt_provider.h"
#include "tile/url_tile_provider.h"
#include "ui/hardware_profiles.h"
#include "analysis/viewshed.h"
#include "util/math_util.h"
#include "util/color.h"
#include "util/log.h"
#include "scene/node_marker.h"
#include "scene/signal_sphere.h"
#include <glad/glad.h>
#include <cstring>
#include <filesystem>
#include <cmath>

namespace mesh3d {

static App g_app;
App& app() { return g_app; }

std::string App::find_font_path() {
    namespace fs = std::filesystem;
    /* Search relative to shader dir, then common locations */
    for (auto& candidate : {
        "assets/fonts/LiberationMono-Regular.ttf",
        "../assets/fonts/LiberationMono-Regular.ttf",
        "../../assets/fonts/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/liberation-mono/LiberationMono-Regular.ttf",
    }) {
        if (fs::exists(candidate)) return candidate;
    }
    return "assets/fonts/LiberationMono-Regular.ttf"; // fallback
}

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

    /* Initialize HUD */
    std::string font_path = find_font_path();
    if (!m_hud.init(m_shader_dir, font_path)) {
        LOG_WARN("HUD init failed (font: %s) — HUD disabled", font_path.c_str());
    }

    /* Default camera position */
    camera.position = glm::vec3(0, 500, 200);
    camera.rotate(0, 0); // force vector update

    LOG_INFO("mesh3d initialized (%dx%d)", width, height);
    return true;
}

void App::shutdown() {
    m_hud.shutdown();
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
    m_proj.init(bounds);
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
    /* ESC handling — menu toggle or exit node placement */
    if (m_input.consume_escape()) {
        if (m_node_placement_mode) {
            m_node_placement_mode = false;
            LOG_INFO("Exited node placement mode");
        } else if (m_hud.menu().open) {
            m_hud.menu().open = false;
            m_input.set_menu_open(false);
            SDL_StopTextInput();
            LOG_INFO("Menu closed");
        } else {
            m_hud.menu().open = true;
            m_input.set_menu_open(true);
            SDL_StartTextInput();
            LOG_INFO("Menu opened");
        }
    }

    /* Don't process normal toggles when menu is open */
    if (m_hud.menu().open) {
        handle_menu_input();
        return;
    }

    if (m_input.consume_tab()) {
        set_render_mode(scene.render_mode == MESH3D_MODE_TERRAIN
                        ? MESH3D_MODE_FLAT : MESH3D_MODE_TERRAIN);
    }
    if (m_input.consume_key1()) {
        /* Cycle overlay: none -> viewshed -> signal -> none */
        int next = (static_cast<int>(scene.overlay_mode) + 1) % 3;
        set_overlay_mode(static_cast<mesh3d_overlay_mode_t>(next));
    }
    if (m_input.consume_key3()) cycle_imagery_source();
    if (m_input.consume_keyT()) toggle_signal_spheres();
    if (m_input.consume_keyF()) toggle_wireframe();

    if (m_input.consume_keyH()) {
        m_show_controls = !m_show_controls;
        LOG_INFO("Controls display: %s", m_show_controls ? "ON" : "OFF");
    }

    if (m_input.consume_keyN()) {
        m_node_placement_mode = !m_node_placement_mode;
        if (m_node_placement_mode) {
            /* Drain any stale click events so mouselook clicks
               don't get misinterpreted as placement actions. */
            m_input.consume_left_click();
            m_input.consume_right_click();
            m_input.consume_delete_key();
        }
        LOG_INFO("Node placement mode: %s", m_node_placement_mode ? "ON" : "OFF");
    }

    /* Node placement interactions */
    if (m_node_placement_mode) {
        handle_node_placement();
    }
}

void App::handle_menu_input() {
    /* Text input */
    char tc = m_input.consume_text_char();
    if (tc) m_hud.menu_text_input(tc);

    if (m_input.consume_backspace()) m_hud.menu_backspace();
    if (m_input.consume_arrow_up()) m_hud.menu_navigate(-1);
    if (m_input.consume_arrow_down()) m_hud.menu_navigate(1);
    if (m_input.consume_arrow_left()) m_hud.menu_device_left();
    if (m_input.consume_arrow_right()) m_hud.menu_device_right();

    if (m_input.consume_enter()) {
        int result = m_hud.menu_activate(scene, camera, m_proj);
        if (result == 1) {
            // Resume
            m_hud.menu().open = false;
            m_input.set_menu_open(false);
            SDL_StopTextInput();
            LOG_INFO("Menu: resumed");
        } else if (result == 2) {
            // Quit
            LOG_INFO("Menu: quit requested");
            m_hud.menu().open = false;
            m_input.set_menu_open(false);
            SDL_StopTextInput();
            // Push a quit event
            SDL_Event quit_ev;
            quit_ev.type = SDL_QUIT;
            SDL_PushEvent(&quit_ev);
        }
    }

    /* Delete node from menu */
    if (m_input.consume_delete_key()) {
        auto& menu = m_hud.menu();
        int node_idx = -1;
        if (m_hud.is_node_field(menu.focused_field, scene, node_idx)) {
            if (node_idx >= 0 && node_idx < (int)scene.nodes.size()) {
                scene.nodes.erase(scene.nodes.begin() + node_idx);
                scene.build_markers();
                scene.build_spheres();
                recompute_all_viewsheds(scene, m_proj);
                menu.editing_node = -1;
                menu.device_select_node = -1;
                LOG_INFO("Deleted node %d from menu", node_idx);
            }
        }
    }
}

void App::handle_node_placement() {
    if (m_input.consume_left_click()) {
        auto hit = raycast_terrain();
        if (hit) {
            place_node_at(*hit);
        }
    }
    if (m_input.consume_right_click()) {
        // Right-click in placement mode: delete nearest node
        // (only if not captured for mouselook — but we consume the flag anyway)
        auto hit = raycast_terrain();
        if (hit) {
            delete_nearest_node(*hit);
        }
    }
    if (m_input.consume_delete_key()) {
        auto hit = raycast_terrain();
        if (hit) {
            delete_nearest_node(*hit);
        }
    }
}

std::optional<glm::vec3> App::raycast_terrain() {
    /* Ray march along camera front direction, sampling elevation grid */
    if (scene.elevation.empty() || scene.grid_rows < 2 || scene.grid_cols < 2) {
        /* No scene-level grid — sample from tile system */
        glm::vec3 origin = camera.position;
        glm::vec3 dir = camera.front();
        if (dir.y >= 0) return std::nullopt; // looking up

        /* Estimate max distance from a ground-plane intersection */
        float t_ground = -origin.y / dir.y;
        if (t_ground < 0) return std::nullopt;
        float max_dist = std::min(t_ground * 2.0f, 50000.0f);

        float step = 10.0f;
        for (float t = 0; t < max_dist; t += step) {
            glm::vec3 p = origin + dir * t;
            float terrain_h = scene.tile_manager.get_elevation_at(
                p.x, p.z, m_proj);
            if (p.y <= terrain_h) {
                /* Binary search refinement */
                float lo = std::max(0.0f, t - step);
                float hi = t;
                for (int i = 0; i < 10; ++i) {
                    float mid = (lo + hi) * 0.5f;
                    glm::vec3 mp = origin + dir * mid;
                    float mh = scene.tile_manager.get_elevation_at(
                        mp.x, mp.z, m_proj);
                    if (mp.y <= mh) hi = mid;
                    else lo = mid;
                }
                return origin + dir * ((lo + hi) * 0.5f);
            }
        }
        return std::nullopt;
    }

    /* Grid-based elevation sampling */
    glm::vec3 origin = camera.position;
    glm::vec3 dir = camera.front();
    if (dir.y >= 0) return std::nullopt;

    float width_m = m_proj.width_m(scene.bounds);
    float height_m = m_proj.height_m(scene.bounds);
    float half_w = width_m * 0.5f;
    float half_h = height_m * 0.5f;

    float step = std::min(width_m, height_m) / std::max(scene.grid_rows, scene.grid_cols);
    step = std::max(step, 1.0f);
    float max_dist = 100000.0f;

    for (float t = 0; t < max_dist; t += step) {
        glm::vec3 p = origin + dir * t;

        /* Convert world XZ to grid coords */
        float gx = (p.x + half_w) / width_m * (scene.grid_cols - 1);
        float gz = (p.z + half_h) / height_m * (scene.grid_rows - 1);

        int ix = static_cast<int>(gx);
        int iz = static_cast<int>(gz);
        if (ix < 0 || ix >= scene.grid_cols - 1 || iz < 0 || iz >= scene.grid_rows - 1)
            continue;

        /* Bilinear interpolation of elevation */
        float fx = gx - ix;
        float fz = gz - iz;
        float e00 = scene.elevation[iz * scene.grid_cols + ix];
        float e10 = scene.elevation[iz * scene.grid_cols + ix + 1];
        float e01 = scene.elevation[(iz + 1) * scene.grid_cols + ix];
        float e11 = scene.elevation[(iz + 1) * scene.grid_cols + ix + 1];
        float terrain_y = e00 * (1 - fx) * (1 - fz) + e10 * fx * (1 - fz)
                        + e01 * (1 - fx) * fz + e11 * fx * fz;

        if (p.y <= terrain_y) {
            /* Binary search refinement */
            float lo = std::max(0.0f, t - step);
            float hi = t;
            for (int i = 0; i < 10; ++i) {
                float mid = (lo + hi) * 0.5f;
                glm::vec3 mp = origin + dir * mid;
                float mgx = (mp.x + half_w) / width_m * (scene.grid_cols - 1);
                float mgz = (mp.z + half_h) / height_m * (scene.grid_rows - 1);
                int mix = std::clamp((int)mgx, 0, scene.grid_cols - 2);
                int miz = std::clamp((int)mgz, 0, scene.grid_rows - 2);
                float mfx = mgx - mix;
                float mfz = mgz - miz;
                float me00 = scene.elevation[miz * scene.grid_cols + mix];
                float me10 = scene.elevation[miz * scene.grid_cols + mix + 1];
                float me01 = scene.elevation[(miz + 1) * scene.grid_cols + mix];
                float me11 = scene.elevation[(miz + 1) * scene.grid_cols + mix + 1];
                float mh = me00 * (1 - mfx) * (1 - mfz) + me10 * mfx * (1 - mfz)
                          + me01 * (1 - mfx) * mfz + me11 * mfx * mfz;
                if (mp.y <= mh) hi = mid;
                else lo = mid;
            }
            return origin + dir * ((lo + hi) * 0.5f);
        }
    }
    return std::nullopt;
}

void App::place_node_at(const glm::vec3& world_pos) {
    auto ll = m_proj.unproject(world_pos.x, world_pos.z);

    /* Use default hardware profile (heltec_v3) */
    const auto& hp = HARDWARE_PROFILES[0];

    mesh3d_node_t node{};
    int idx = static_cast<int>(scene.nodes.size());
    snprintf(node.name, sizeof(node.name), "Node-%d", idx + 1);
    node.id = idx + 1;
    node.lat = ll.lat;
    node.lon = ll.lon;
    node.alt = world_pos.y;
    node.antenna_height_m = 2.0f;
    node.role = 1; // relay
    node.max_range_km = hp.max_range_km;
    node.tx_power_dbm = hp.tx_power_dbm;
    node.antenna_gain_dbi = hp.antenna_gain_dbi;
    node.rx_sensitivity_dbm = hp.rx_sensitivity_dbm;
    node.frequency_mhz = hp.frequency_mhz;

    NodeData nd;
    nd.info = node;
    nd.world_pos = glm::vec3(world_pos.x, world_pos.y + node.antenna_height_m, world_pos.z);
    scene.nodes.push_back(nd);

    /* Rebuild markers, spheres, and viewsheds */
    scene.build_markers();
    scene.build_spheres();
    recompute_all_viewsheds(scene, m_proj);

    LOG_INFO("Placed node '%s' at (%.4f, %.4f, %.0fm)", node.name, ll.lat, ll.lon, world_pos.y);
}

void App::delete_nearest_node(const glm::vec3& world_pos) {
    if (scene.nodes.empty()) return;

    float min_dist = std::numeric_limits<float>::max();
    int nearest = -1;

    for (int i = 0; i < (int)scene.nodes.size(); ++i) {
        float d = glm::length(scene.nodes[i].world_pos - world_pos);
        if (d < min_dist) {
            min_dist = d;
            nearest = i;
        }
    }

    /* Threshold: within 500m (reasonable for typical mesh node spacing) */
    if (nearest >= 0 && min_dist < 500.0f) {
        LOG_INFO("Deleted node '%s'", scene.nodes[nearest].info.name);
        scene.nodes.erase(scene.nodes.begin() + nearest);
        scene.build_markers();
        scene.build_spheres();
        recompute_all_viewsheds(scene, m_proj);
    }
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
    renderer.render(scene, camera, aspect,
                    m_width, m_height,
                    &m_hud, &m_proj,
                    m_node_placement_mode, m_show_controls);

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
