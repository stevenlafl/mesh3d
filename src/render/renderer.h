#pragma once
#include <glad/glad.h>
#include <string>
#include "render/shader.h"
#include <mesh3d/types.h>

namespace mesh3d {

class Scene;
class Camera;
class Hud;
struct GeoProjection;

class Renderer {
public:
    bool init(const std::string& shader_dir);
    void render(const Scene& scene, const Camera& cam, float aspect,
                int screen_w, int screen_h,
                Hud* hud, const GeoProjection* proj,
                bool node_placement_mode, bool show_controls);

    void set_wireframe(bool on);
    bool wireframe() const { return m_wireframe; }

    Shader terrain_shader;
    Shader flat_shader;
    Shader marker_shader;
    Shader sphere_shader;

private:
    bool m_wireframe = false;
    std::string m_shader_dir;

    void opaque_pass(const Scene& scene, const Camera& cam, float aspect);
    void transparent_pass(const Scene& scene, const Camera& cam, float aspect);
    void hud_pass(const Scene& scene, const Camera& cam,
                  int screen_w, int screen_h,
                  Hud* hud, const GeoProjection* proj,
                  bool node_placement_mode, bool show_controls);
    void setup_common_uniforms(Shader& s, const Camera& cam, float aspect);
};

} // namespace mesh3d
