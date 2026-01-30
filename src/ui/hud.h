#pragma once
#include "render/shader.h"
#include "render/mesh.h"
#include "render/texture.h"
#include "scene/scene.h"
#include "camera/camera.h"
#include "ui/hardware_profiles.h"
#include "util/math_util.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace mesh3d {

/* Per-glyph metrics from stb_truetype bake */
struct GlyphInfo {
    float x0, y0, x1, y1; // quad coords (offset from cursor)
    float u0, v0, u1, v1; // texture coords
    float advance;         // horizontal advance
};

/* Menu field types */
enum class MenuField {
    JUMP_LAT,
    JUMP_LON,
    JUMP_BUTTON,
    CAM_SPEED,
    CAM_SPEED_BUTTON,
    NODE_LIST_START, // first node row; add node_index to get specific row
    RESUME,
    QUIT
};

struct MenuState {
    bool open = false;

    /* Navigation input fields */
    std::string lat_input;
    std::string lon_input;
    std::string speed_input;
    bool lat_active = false;
    bool lon_active = false;
    bool speed_active = false;

    /* Node list */
    int scroll_offset = 0;
    int editing_node = -1;        // index into scene.nodes, -1 = none
    int device_select_node = -1;  // node being device-selected
    int device_select_idx = 0;    // index in HARDWARE_PROFILES

    /* Focus tracking */
    int focused_field = 0; // simple linear index for arrow key nav
    static constexpr int FIELD_COUNT_FIXED = 7; // lat, lon, jump, speed, apply, resume, quit
};

class Hud {
public:
    bool init(const std::string& shader_dir, const std::string& font_path);
    void shutdown();

    /* Per-frame rendering */
    void render(int screen_w, int screen_h,
                const Scene& scene, const Camera& cam,
                const GeoProjection& proj,
                bool node_placement_mode, bool show_controls);

    /* Menu management */
    MenuState& menu() { return m_menu; }
    const MenuState& menu() const { return m_menu; }

    /* Text input handling (called from App when menu is open) */
    void menu_text_input(char c);
    void menu_backspace();
    void menu_navigate(int dir); // +1 = down, -1 = up
    int  menu_activate(Scene& scene, Camera& cam, GeoProjection& proj);
    // returns: 0=nothing, 1=resume, 2=quit, 3=action taken (stay open)

    /* Device selection in menu */
    void menu_device_left();
    void menu_device_right();

private:
    Shader  m_shader;
    GLuint  m_font_tex = 0;
    Mesh    m_quad_mesh; // reusable dynamic quad
    GLuint  m_quad_vao = 0, m_quad_vbo = 0;

    GlyphInfo m_glyphs[128]; // ASCII
    float m_font_size = 16.0f;
    float m_line_height = 20.0f;
    float m_ascent = 12.0f; // pixels above baseline (set from font metrics)
    int   m_atlas_w = 512, m_atlas_h = 512;

    MenuState m_menu;

    /* Drawing helpers */
    void draw_text(const std::string& text, float x, float y,
                   const glm::vec4& color, float scale, int screen_w, int screen_h);
    void draw_rect(float x, float y, float w, float h,
                   const glm::vec4& color, int screen_w, int screen_h);
    void draw_crosshair(int screen_w, int screen_h);
    void draw_controls(int screen_w, int screen_h);
    void draw_mode_indicator(int screen_w, int screen_h, bool node_placement_mode);
    void draw_menu(int screen_w, int screen_h, const Scene& scene,
                   const Camera& cam, const GeoProjection& proj);

    void upload_quad(float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1);
    glm::mat4 ortho_proj(int screen_w, int screen_h);

public:
    /* Menu field helpers */
    int total_menu_fields(const Scene& scene) const;
    bool is_node_field(int field, const Scene& scene, int& node_idx) const;

private:
};

} // namespace mesh3d
