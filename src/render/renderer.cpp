#include "render/renderer.h"
#include "scene/scene.h"
#include "tile/tile_manager.h"
#include "camera/camera.h"
#include "ui/hud.h"
#include "util/log.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace mesh3d {

bool Renderer::init(const std::string& shader_dir) {
    m_shader_dir = shader_dir;

    if (!terrain_shader.load(shader_dir + "/terrain.vert", shader_dir + "/terrain.frag")) {
        LOG_ERROR("Failed to load terrain shader");
        return false;
    }
    if (!flat_shader.load(shader_dir + "/flat.vert", shader_dir + "/flat.frag")) {
        LOG_ERROR("Failed to load flat shader");
        return false;
    }
    if (!marker_shader.load(shader_dir + "/marker.vert", shader_dir + "/marker.frag")) {
        LOG_ERROR("Failed to load marker shader");
        return false;
    }
    if (!sphere_shader.load(shader_dir + "/sphere.vert", shader_dir + "/sphere.frag")) {
        LOG_ERROR("Failed to load sphere shader");
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    LOG_INFO("Renderer initialized");
    return true;
}

void Renderer::set_wireframe(bool on) {
    m_wireframe = on;
    glPolygonMode(GL_FRONT_AND_BACK, on ? GL_LINE : GL_FILL);
}

void Renderer::setup_common_uniforms(Shader& s, const Camera& cam, float aspect) {
    s.use();
    s.set_mat4("uView", cam.view_matrix());
    s.set_mat4("uProj", cam.projection_matrix(aspect));
    s.set_vec3("uCameraPos", cam.position);
}

void Renderer::render(const Scene& scene, const Camera& cam, float aspect,
                       int screen_w, int screen_h,
                       Hud* hud, const GeoProjection* proj,
                       bool node_placement_mode, bool show_controls) {
    glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    opaque_pass(scene, cam, aspect);
    transparent_pass(scene, cam, aspect);
    hud_pass(scene, cam, screen_w, screen_h, hud, proj, node_placement_mode, show_controls);
}

void Renderer::hud_pass(const Scene& scene, const Camera& cam,
                         int screen_w, int screen_h,
                         Hud* hud, const GeoProjection* proj,
                         bool node_placement_mode, bool show_controls) {
    if (!hud || !proj) return;
    hud->render(screen_w, screen_h, scene, cam, *proj,
                node_placement_mode, show_controls);
}

void Renderer::opaque_pass(const Scene& scene, const Camera& cam, float aspect) {
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    /* Terrain or flat plane */
    if (scene.render_mode == MESH3D_MODE_TERRAIN && scene.use_tile_system &&
        scene.tile_manager.has_terrain()) {
        /* Tile-based terrain rendering */
        setup_common_uniforms(terrain_shader, cam, aspect);
        terrain_shader.set_int("uOverlayMode", static_cast<int>(scene.overlay_mode));
        terrain_shader.set_vec3("uLightDir", glm::normalize(glm::vec3(0.3f, 1.0f, 0.5f)));
        terrain_shader.set_float("uRxSensitivity", scene.rf_config.rx_sensitivity_dbm);
        terrain_shader.set_float("uDisplayMinDbm", scene.rf_config.display_min_dbm);
        terrain_shader.set_float("uDisplayMaxDbm", scene.rf_config.display_max_dbm);
        const_cast<TileManager&>(scene.tile_manager).render([&](const TileRenderable& tile) {
            terrain_shader.set_mat4("uModel", tile.model);
            terrain_shader.set_int("uUseSatelliteTex", tile.texture.valid() ? 1 : 0);
            if (tile.texture.valid()) {
                tile.texture.bind(0);
                terrain_shader.set_int("uSatelliteTex", 0);
            }
            /* Bind GPU overlay textures if available (avoids mesh rebuild) */
            terrain_shader.set_int("uUseOverlayTex", tile.overlay_tex_valid ? 1 : 0);
            if (tile.overlay_tex_valid) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, tile.overlay_vis_tex);
                terrain_shader.set_int("uOverlayVisTex", 1);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, tile.overlay_sig_tex);
                terrain_shader.set_int("uOverlaySigTex", 2);
                glActiveTexture(GL_TEXTURE0);
            }
            tile.mesh.draw();
        });
    } else if (scene.render_mode == MESH3D_MODE_TERRAIN && scene.terrain_mesh.valid()) {
        setup_common_uniforms(terrain_shader, cam, aspect);
        terrain_shader.set_mat4("uModel", scene.terrain_model);
        terrain_shader.set_int("uOverlayMode", static_cast<int>(scene.overlay_mode));
        terrain_shader.set_int("uUseSatelliteTex", scene.satellite_tex.valid() ? 1 : 0);
        terrain_shader.set_int("uUseOverlayTex", 0);
        terrain_shader.set_vec3("uLightDir", glm::normalize(glm::vec3(0.3f, 1.0f, 0.5f)));
        terrain_shader.set_float("uRxSensitivity", scene.rf_config.rx_sensitivity_dbm);
        terrain_shader.set_float("uDisplayMinDbm", scene.rf_config.display_min_dbm);
        terrain_shader.set_float("uDisplayMaxDbm", scene.rf_config.display_max_dbm);
        if (scene.satellite_tex.valid()) {
            scene.satellite_tex.bind(0);
            terrain_shader.set_int("uSatelliteTex", 0);
        }
        scene.terrain_mesh.draw();
    } else if (scene.render_mode == MESH3D_MODE_FLAT && scene.flat_mesh.valid()) {
        setup_common_uniforms(flat_shader, cam, aspect);
        flat_shader.set_mat4("uModel", scene.flat_model);
        flat_shader.set_int("uOverlayMode", static_cast<int>(scene.overlay_mode));
        scene.flat_mesh.draw();
    }

    /* Node markers */
    if (!scene.marker_meshes.empty()) {
        setup_common_uniforms(marker_shader, cam, aspect);
        for (size_t i = 0; i < scene.marker_meshes.size(); ++i) {
            if (!scene.marker_meshes[i].valid()) continue;
            marker_shader.set_mat4("uModel", scene.marker_models[i]);
            marker_shader.set_vec3("uColor", scene.marker_colors[i]);
            scene.marker_meshes[i].draw();
        }
    }
}

void Renderer::transparent_pass(const Scene& scene, const Camera& cam, float aspect) {
    if (!scene.show_signal_spheres || scene.sphere_meshes.empty()) return;

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    setup_common_uniforms(sphere_shader, cam, aspect);

    /* Sort back-to-front */
    std::vector<size_t> order(scene.sphere_meshes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        float da = glm::length(cam.position - scene.sphere_centers[a]);
        float db = glm::length(cam.position - scene.sphere_centers[b]);
        return da > db; // back first
    });

    for (size_t idx : order) {
        if (!scene.sphere_meshes[idx].valid()) continue;
        sphere_shader.set_mat4("uModel", scene.sphere_models[idx]);
        sphere_shader.set_vec3("uColor", scene.sphere_colors[idx]);
        sphere_shader.set_float("uBaseAlpha", 0.18f);
        scene.sphere_meshes[idx].draw();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace mesh3d
