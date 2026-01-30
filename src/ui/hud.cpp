#include "ui/hud.h"
#include "util/log.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <filesystem>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace mesh3d {

bool Hud::init(const std::string& shader_dir, const std::string& font_path) {
    /* Load HUD shader */
    if (!m_shader.load(shader_dir + "/hud.vert", shader_dir + "/hud.frag")) {
        LOG_ERROR("Failed to load HUD shader");
        return false;
    }

    /* Load font file */
    FILE* f = fopen(font_path.c_str(), "rb");
    if (!f) {
        LOG_ERROR("Failed to open font: %s", font_path.c_str());
        return false;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> font_data(fsize);
    fread(font_data.data(), 1, fsize, f);
    fclose(f);

    /* Bake ASCII glyphs into atlas */
    std::vector<unsigned char> atlas(m_atlas_w * m_atlas_h);
    stbtt_bakedchar cdata[96]; // ASCII 32..127

    int result = stbtt_BakeFontBitmap(font_data.data(), 0, m_font_size,
                                       atlas.data(), m_atlas_w, m_atlas_h,
                                       32, 96, cdata);
    if (result <= 0) {
        LOG_WARN("Font bake returned %d (may need larger atlas)", result);
    }

    /* Compute font ascent from stb_truetype metrics */
    stbtt_fontinfo font_info;
    stbtt_InitFont(&font_info, font_data.data(),
                   stbtt_GetFontOffsetForIndex(font_data.data(), 0));
    float scale_factor = stbtt_ScaleForPixelHeight(&font_info, m_font_size);
    int ascent_i, descent_i, linegap_i;
    stbtt_GetFontVMetrics(&font_info, &ascent_i, &descent_i, &linegap_i);
    m_ascent = ascent_i * scale_factor;

    /* Store glyph metrics */
    std::memset(m_glyphs, 0, sizeof(m_glyphs));
    for (int i = 0; i < 96; ++i) {
        int ch = 32 + i;
        auto& g = m_glyphs[ch];
        auto& c = cdata[i];
        g.x0 = c.xoff;
        g.y0 = c.yoff;
        g.x1 = c.xoff + (c.x1 - c.x0);
        g.y1 = c.yoff + (c.y1 - c.y0);
        g.u0 = static_cast<float>(c.x0) / m_atlas_w;
        g.v0 = static_cast<float>(c.y0) / m_atlas_h;
        g.u1 = static_cast<float>(c.x1) / m_atlas_w;
        g.v1 = static_cast<float>(c.y1) / m_atlas_h;
        g.advance = c.xadvance;
    }

    /* Upload atlas as single-channel texture */
    glGenTextures(1, &m_font_tex);
    glBindTexture(GL_TEXTURE_2D, m_font_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_atlas_w, m_atlas_h, 0,
                 GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Create dynamic quad VAO/VBO (reused for all HUD draws) */
    glGenVertexArrays(1, &m_quad_vao);
    glGenBuffers(1, &m_quad_vbo);
    glBindVertexArray(m_quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
    // aPos (location 0) = vec2
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // aUV (location 1) = vec2
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    LOG_INFO("HUD initialized (font atlas %dx%d)", m_atlas_w, m_atlas_h);
    return true;
}

void Hud::shutdown() {
    if (m_font_tex) { glDeleteTextures(1, &m_font_tex); m_font_tex = 0; }
    if (m_quad_vbo) { glDeleteBuffers(1, &m_quad_vbo); m_quad_vbo = 0; }
    if (m_quad_vao) { glDeleteVertexArrays(1, &m_quad_vao); m_quad_vao = 0; }
}

glm::mat4 Hud::ortho_proj(int screen_w, int screen_h) {
    return glm::ortho(0.0f, static_cast<float>(screen_w),
                      static_cast<float>(screen_h), 0.0f,
                      -1.0f, 1.0f);
}

void Hud::upload_quad(float x, float y, float w, float h,
                       float u0, float v0, float u1, float v1) {
    float verts[] = {
        x,     y,     u0, v0,
        x + w, y,     u1, v0,
        x + w, y + h, u1, v1,

        x,     y,     u0, v0,
        x + w, y + h, u1, v1,
        x,     y + h, u0, v1,
    };
    glBindBuffer(GL_ARRAY_BUFFER, m_quad_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
}

void Hud::draw_rect(float x, float y, float w, float h,
                     const glm::vec4& color, int screen_w, int screen_h) {
    m_shader.use();
    m_shader.set_mat4("uProj", ortho_proj(screen_w, screen_h));
    m_shader.set_vec4("uColor", color);
    m_shader.set_int("uUseTexture", 0);

    upload_quad(x, y, w, h, 0, 0, 1, 1);
    glBindVertexArray(m_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Hud::draw_text(const std::string& text, float x, float y,
                     const glm::vec4& color, float scale,
                     int screen_w, int screen_h) {
    m_shader.use();
    m_shader.set_mat4("uProj", ortho_proj(screen_w, screen_h));
    m_shader.set_vec4("uColor", color);
    m_shader.set_int("uUseTexture", 1);
    m_shader.set_int("uTex", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_font_tex);
    glBindVertexArray(m_quad_vao);

    float cursor_x = x;
    /* y is the top of the text line; offset by ascent to get the baseline
       that stb_truetype glyph offsets are relative to. */
    float baseline_y = y + m_ascent * scale;

    for (char ch : text) {
        if (ch < 32 || ch > 126) {
            if (ch == '\n') {
                cursor_x = x;
                baseline_y += m_line_height * scale;
            }
            continue;
        }
        auto& g = m_glyphs[(int)ch];
        float qx = cursor_x + g.x0 * scale;
        float qy = baseline_y + g.y0 * scale;
        float qw = (g.x1 - g.x0) * scale;
        float qh = (g.y1 - g.y0) * scale;

        upload_quad(qx, qy, qw, qh, g.u0, g.v0, g.u1, g.v1);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        cursor_x += g.advance * scale;
    }
}

float Hud::measure_text(const std::string& text, float scale) {
    float w = 0.0f;
    for (char ch : text) {
        if (ch < 32 || ch > 126) continue;
        w += m_glyphs[(int)ch].advance * scale;
    }
    return w;
}

void Hud::draw_text_shadowed(const std::string& text, float x, float y,
                              const glm::vec4& color, float scale,
                              int screen_w, int screen_h) {
    /* Dark shadow offset by 1px */
    glm::vec4 shadow(0.0f, 0.0f, 0.0f, color.a * 0.8f);
    draw_text(text, x + 1.0f, y + 1.0f, shadow, scale, screen_w, screen_h);
    draw_text(text, x, y, color, scale, screen_w, screen_h);
}

void Hud::draw_crosshair(int screen_w, int screen_h) {
    float cx = screen_w * 0.5f;
    float cy = screen_h * 0.5f;
    float size = 12.0f;
    float thick = 2.0f;
    glm::vec4 color(1.0f, 1.0f, 1.0f, 0.8f);

    // horizontal line
    draw_rect(cx - size, cy - thick * 0.5f, size * 2, thick, color, screen_w, screen_h);
    // vertical line
    draw_rect(cx - thick * 0.5f, cy - size, thick, size * 2, color, screen_w, screen_h);
    // center dot
    draw_rect(cx - 1.5f, cy - 1.5f, 3.0f, 3.0f, glm::vec4(1, 0.3f, 0.3f, 1.0f), screen_w, screen_h);
}

void Hud::draw_mode_indicator(int screen_w, int screen_h, bool node_placement_mode) {
    if (!node_placement_mode) return;

    float banner_w = 240.0f;
    float banner_h = 30.0f;
    float bx = (screen_w - banner_w) * 0.5f;
    float by = 36.0f; // below top-left info text

    draw_rect(bx, by, banner_w, banner_h, glm::vec4(0.8f, 0.2f, 0.1f, 0.85f), screen_w, screen_h);
    float tw = measure_text("NODE PLACEMENT", 1.0f);
    float tx = bx + (banner_w - tw) * 0.5f;
    float ty = by + (banner_h - m_line_height) * 0.5f;
    draw_text("NODE PLACEMENT", tx, ty,
              glm::vec4(1, 1, 1, 1), 1.0f, screen_w, screen_h);
}

void Hud::draw_controls(int screen_w, int screen_h) {
    float x = 10.0f;
    float y = screen_h - 280.0f;
    float bg_w = 280.0f;
    float bg_h = 270.0f;

    draw_rect(x, y, bg_w, bg_h, glm::vec4(0.0f, 0.0f, 0.0f, 0.65f), screen_w, screen_h);

    glm::vec4 hdr(0.4f, 0.8f, 1.0f, 1.0f);
    glm::vec4 txt(0.85f, 0.85f, 0.85f, 1.0f);
    float lx = x + 10.0f;
    float ly = y + 8.0f;
    float lh = m_line_height;

    draw_text("Controls", lx, ly, hdr, 1.0f, screen_w, screen_h); ly += lh + 4;
    draw_text("WASD     Move camera",   lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("Q / E    Down / Up",     lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("RMB      Mouselook",     lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("Scroll   FOV zoom",      lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("Shift    Sprint (4x)",   lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("Tab      Terrain/Flat",  lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("1        Cycle overlay", lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("3        Cycle imagery", lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("T        Spheres",       lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("F        Wireframe",     lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("N        Place nodes",   lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("H        Toggle help",   lx, ly, txt, 1.0f, screen_w, screen_h); ly += lh;
    draw_text("ESC      Menu",          lx, ly, txt, 1.0f, screen_w, screen_h);
}

void Hud::draw_menu(int screen_w, int screen_h, const Scene& scene,
                     const Camera& cam, const GeoProjection& proj) {
    /* Dark backdrop */
    draw_rect(0, 0, (float)screen_w, (float)screen_h,
              glm::vec4(0, 0, 0, 0.7f), screen_w, screen_h);

    /* Menu panel */
    float pw = 600.0f;
    float ph = 500.0f;
    float max_nodes_display = 8;
    float node_list_h = std::min((float)scene.nodes.size(), max_nodes_display) * m_line_height + 40.0f;
    ph = std::max(ph, 260.0f + node_list_h);

    float px = (screen_w - pw) * 0.5f;
    float py = (screen_h - ph) * 0.5f;

    draw_rect(px, py, pw, ph, glm::vec4(0.12f, 0.14f, 0.18f, 0.95f), screen_w, screen_h);
    draw_rect(px, py, pw, 2.0f, glm::vec4(0.3f, 0.6f, 1.0f, 1.0f), screen_w, screen_h);

    glm::vec4 hdr(0.4f, 0.8f, 1.0f, 1.0f);
    glm::vec4 lbl(0.7f, 0.7f, 0.7f, 1.0f);
    glm::vec4 val(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 sel(0.2f, 0.5f, 1.0f, 0.5f); // selection highlight
    glm::vec4 btn(0.3f, 0.6f, 1.0f, 1.0f);

    float lx = px + 20.0f;
    float ly = py + 20.0f;
    float lh = m_line_height + 4.0f;
    int field = 0;

    /* Title */
    draw_text("MESH3D MENU", lx, ly, hdr, 1.2f, screen_w, screen_h);
    ly += lh * 1.5f;

    /* Camera position info */
    auto ll = proj.unproject(cam.position.x, cam.position.z);
    char buf[128];
    snprintf(buf, sizeof(buf), "Camera: %.4f, %.4f  Alt: %.0fm",
             ll.lat, ll.lon, cam.position.y);
    draw_text(buf, lx, ly, lbl, 1.0f, screen_w, screen_h);
    ly += lh * 1.3f;

    /* Navigation section */
    draw_text("Navigation", lx, ly, hdr, 1.0f, screen_w, screen_h);
    ly += lh;

    // Lat field
    if (m_menu.focused_field == field)
        draw_rect(lx - 4, ly - 4, pw - 40, lh, sel, screen_w, screen_h);
    {
        std::string disp = "Lat: [" + (m_menu.lat_input.empty() ? "___" : m_menu.lat_input);
        if (m_menu.lat_active) disp += "_";
        disp += "]";
        draw_text(disp, lx, ly, m_menu.lat_active ? val : lbl, 1.0f, screen_w, screen_h);
    }
    ly += lh; field++;

    // Lon field
    if (m_menu.focused_field == field)
        draw_rect(lx - 4, ly - 4, pw - 40, lh, sel, screen_w, screen_h);
    {
        std::string disp = "Lon: [" + (m_menu.lon_input.empty() ? "___" : m_menu.lon_input);
        if (m_menu.lon_active) disp += "_";
        disp += "]";
        draw_text(disp, lx, ly, m_menu.lon_active ? val : lbl, 1.0f, screen_w, screen_h);
    }
    ly += lh; field++;

    // Jump button
    if (m_menu.focused_field == field)
        draw_rect(lx - 4, ly - 4, 80, lh, sel, screen_w, screen_h);
    draw_text("[Jump]", lx, ly, btn, 1.0f, screen_w, screen_h);
    ly += lh * 1.2f; field++;

    // Speed field
    if (m_menu.focused_field == field)
        draw_rect(lx - 4, ly - 4, pw - 40, lh, sel, screen_w, screen_h);
    {
        std::string disp = "Camera Speed: [" + (m_menu.speed_input.empty() ? "___" : m_menu.speed_input);
        if (m_menu.speed_active) disp += "_";
        snprintf(buf, sizeof(buf), "]  (current: %.0f)", cam.move_speed);
        disp += buf;
        draw_text(disp, lx, ly, m_menu.speed_active ? val : lbl, 1.0f, screen_w, screen_h);
    }
    ly += lh; field++;

    // Apply speed button
    if (m_menu.focused_field == field)
        draw_rect(lx - 4, ly - 4, 80, lh, sel, screen_w, screen_h);
    draw_text("[Apply]", lx, ly, btn, 1.0f, screen_w, screen_h);
    ly += lh * 1.3f; field++;

    /* Node list section */
    draw_text("Nodes", lx, ly, hdr, 1.0f, screen_w, screen_h);
    ly += lh;

    if (scene.nodes.empty()) {
        draw_text("(no nodes placed)", lx, ly, lbl, 1.0f, screen_w, screen_h);
        ly += lh;
    } else {
        // Header row
        draw_text("#  Name         Lat       Lon       Device         Del", lx, ly, lbl, 0.9f, screen_w, screen_h);
        ly += lh;

        int visible_count = std::min((int)scene.nodes.size(), (int)max_nodes_display);
        int start = m_menu.scroll_offset;
        int end = std::min(start + visible_count, (int)scene.nodes.size());

        for (int i = start; i < end; ++i) {
            int node_field = field + (i - start);
            if (m_menu.focused_field == node_field)
                draw_rect(lx - 4, ly - 4, pw - 40, lh, sel, screen_w, screen_h);

            auto& nd = scene.nodes[i];
            // Find device name
            const char* dev_name = "heltec_v3";
            if (m_menu.editing_node == i && m_menu.device_select_node == i) {
                dev_name = HARDWARE_PROFILES[m_menu.device_select_idx].name;
            } else {
                // Match by max_range_km and tx_power
                for (int p = 0; p < HARDWARE_PROFILE_COUNT; ++p) {
                    auto& hp = HARDWARE_PROFILES[p];
                    if (std::abs(hp.max_range_km - nd.info.max_range_km) < 0.1f &&
                        std::abs(hp.tx_power_dbm - nd.info.tx_power_dbm) < 0.1f) {
                        dev_name = hp.name;
                        break;
                    }
                }
            }

            char short_name[13] = {};
            snprintf(short_name, sizeof(short_name), "%.12s", nd.info.name);
            snprintf(buf, sizeof(buf), "%-2d %-12s %8.4f %9.4f  %-14s [X]",
                     i + 1, short_name, nd.info.lat, nd.info.lon, dev_name);
            glm::vec4 row_color = (m_menu.editing_node == i) ?
                glm::vec4(1.0f, 0.9f, 0.3f, 1.0f) : val;
            draw_text(buf, lx, ly, row_color, 0.9f, screen_w, screen_h);

            if (m_menu.editing_node == i && m_menu.device_select_node == i) {
                // Show device selection hint
                ly += lh * 0.8f;
                draw_text("  Left/Right: change device  Enter: confirm",
                          lx, ly, glm::vec4(0.5f, 0.8f, 0.5f, 1.0f), 0.85f, screen_w, screen_h);
            }

            ly += lh;
        }

        if ((int)scene.nodes.size() > visible_count) {
            snprintf(buf, sizeof(buf), "  ... %d more (scroll with arrows)",
                     (int)scene.nodes.size() - visible_count);
            draw_text(buf, lx, ly, lbl, 0.85f, screen_w, screen_h);
            ly += lh;
        }

        field += visible_count;
    }

    ly += lh * 0.5f;

    /* Resume button */
    if (m_menu.focused_field == field)
        draw_rect(lx - 4, ly - 4, 120, lh, sel, screen_w, screen_h);
    draw_text("[Resume] (ESC)", lx, ly, btn, 1.0f, screen_w, screen_h);
    ly += lh; field++;

    /* Quit button */
    if (m_menu.focused_field == field)
        draw_rect(lx - 4, ly - 4, 80, lh, sel, screen_w, screen_h);
    draw_text("[Quit]", lx, ly, glm::vec4(1.0f, 0.4f, 0.3f, 1.0f), 1.0f, screen_w, screen_h);
}

void Hud::draw_signal_scale(int screen_w, int screen_h, const Scene& scene) {
    /* Only show when signal or link margin overlay is active */
    if (scene.overlay_mode != MESH3D_OVERLAY_SIGNAL &&
        scene.overlay_mode != MESH3D_OVERLAY_LINK_MARGIN)
        return;

    float pad = 10.0f;
    float bar_w = 20.0f;
    float bar_h = 200.0f;
    float label_w = 60.0f;
    float total_w = bar_w + label_w + 10.0f;
    float bx = screen_w - pad - total_w;
    float by = pad;

    /* Background */
    draw_rect(bx - 6, by - 6, total_w + 12, bar_h + 32,
              glm::vec4(0.0f, 0.0f, 0.0f, 0.65f), screen_w, screen_h);

    /* Title */
    const char* title = (scene.overlay_mode == MESH3D_OVERLAY_SIGNAL) ? "dBm" : "Margin";
    draw_text(title, bx, by, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), 0.9f, screen_w, screen_h);
    by += m_line_height + 2.0f;

    /* Draw colored bar segments (top = strong, bottom = weak) */
    int segments = 20;
    float seg_h = bar_h / segments;

    if (scene.overlay_mode == MESH3D_OVERLAY_SIGNAL) {
        /* Signal: -80 (top, green) to -130 (bottom, red) */
        for (int i = 0; i < segments; ++i) {
            float t = 1.0f - static_cast<float>(i) / (segments - 1); // 1 at top, 0 at bottom
            glm::vec3 c;
            if (t < 0.5f) {
                c = glm::mix(glm::vec3(1, 0, 0), glm::vec3(1, 1, 0), t * 2.0f);
            } else {
                c = glm::mix(glm::vec3(1, 1, 0), glm::vec3(0, 1, 0), (t - 0.5f) * 2.0f);
            }
            draw_rect(bx, by + i * seg_h, bar_w, seg_h + 1,
                      glm::vec4(c, 1.0f), screen_w, screen_h);
        }

        /* Labels */
        glm::vec4 lbl(0.85f, 0.85f, 0.85f, 1.0f);
        float lx = bx + bar_w + 6.0f;
        draw_text("-80",  lx, by, lbl, 0.85f, screen_w, screen_h);
        draw_text("-105", lx, by + bar_h * 0.5f - m_line_height * 0.5f, lbl, 0.85f, screen_w, screen_h);
        draw_text("-130", lx, by + bar_h - m_line_height, lbl, 0.85f, screen_w, screen_h);
    } else {
        /* Link margin: +20dB (top, green) to 0dB (bottom, red), <0 = black */
        for (int i = 0; i < segments; ++i) {
            float t = 1.0f - static_cast<float>(i) / (segments - 1);
            float margin = t * 20.0f; // 0 to 20
            glm::vec3 c;
            if (margin < 10.0f) {
                c = glm::mix(glm::vec3(1, 0, 0), glm::vec3(1, 1, 0), margin / 10.0f);
            } else {
                c = glm::mix(glm::vec3(1, 1, 0), glm::vec3(0, 1, 0), (margin - 10.0f) / 10.0f);
            }
            draw_rect(bx, by + i * seg_h, bar_w, seg_h + 1,
                      glm::vec4(c, 1.0f), screen_w, screen_h);
        }

        glm::vec4 lbl(0.85f, 0.85f, 0.85f, 1.0f);
        float lx = bx + bar_w + 6.0f;
        draw_text("+20dB", lx, by, lbl, 0.85f, screen_w, screen_h);
        draw_text("+10dB", lx, by + bar_h * 0.5f - m_line_height * 0.5f, lbl, 0.85f, screen_w, screen_h);
        draw_text("0dB",   lx, by + bar_h - m_line_height, lbl, 0.85f, screen_w, screen_h);
    }
}

void Hud::draw_console_log(int screen_w, int screen_h) {
    auto entries = log_recent(3);
    if (entries.empty()) return;

    float pad = 10.0f;
    float line_h = m_line_height;
    float panel_h = entries.size() * line_h + 8.0f;
    float panel_w = 500.0f;
    float px = screen_w - pad - panel_w;
    float py = screen_h - pad - panel_h;

    /* Semi-transparent background */
    draw_rect(px, py, panel_w, panel_h,
              glm::vec4(0.0f, 0.0f, 0.0f, 0.5f), screen_w, screen_h);

    float ty = py + 4.0f;
    for (auto& e : entries) {
        glm::vec4 color;
        switch (e.level) {
            case LogLevel::Error: color = glm::vec4(1.0f, 0.3f, 0.3f, 0.9f); break;
            case LogLevel::Warn:  color = glm::vec4(1.0f, 0.8f, 0.2f, 0.9f); break;
            case LogLevel::Info:  color = glm::vec4(0.7f, 0.7f, 0.7f, 0.7f); break;
            default:              color = glm::vec4(0.5f, 0.5f, 0.5f, 0.6f); break;
        }
        /* Truncate long messages to fit panel */
        std::string msg = e.text;
        if (msg.size() > 70) msg = msg.substr(0, 67) + "...";
        draw_text(msg, px + 6.0f, ty, color, 0.85f, screen_w, screen_h);
        ty += line_h;
    }
}

void Hud::render(int screen_w, int screen_h,
                  const Scene& scene, const Camera& cam,
                  const GeoProjection& proj,
                  bool node_placement_mode, bool show_controls) {
    /* Set GL state for 2D overlay */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Ensure fill mode for HUD even if wireframe is active */
    GLint prev_poly_mode[2];
    glGetIntegerv(GL_POLYGON_MODE, prev_poly_mode);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if (m_menu.open) {
        draw_menu(screen_w, screen_h, scene, cam, proj);
    } else {
        draw_mode_indicator(screen_w, screen_h, node_placement_mode);
        if (node_placement_mode) {
            draw_crosshair(screen_w, screen_h);
        }
        if (show_controls) {
            draw_controls(screen_w, screen_h);
        }

        /* Signal strength color scale (top-right) */
        draw_signal_scale(screen_w, screen_h, scene);
        /* Console log (bottom-right) */
        draw_console_log(screen_w, screen_h);

        /* Always show current position and overlay mode in top-left */
        auto ll = proj.unproject(cam.position.x, cam.position.z);
        char buf[256];
        const char* overlay_name = "None";
        bool has_data = true;
        if (scene.overlay_mode == MESH3D_OVERLAY_VIEWSHED) {
            overlay_name = "Viewshed";
            has_data = !scene.viewshed_vis.empty();
        } else if (scene.overlay_mode == MESH3D_OVERLAY_SIGNAL) {
            overlay_name = "Signal";
            has_data = !scene.signal_strength.empty();
        } else if (scene.overlay_mode == MESH3D_OVERLAY_LINK_MARGIN) {
            overlay_name = "Link Margin";
            has_data = !scene.signal_strength.empty();
        }
        if (scene.overlay_mode == MESH3D_OVERLAY_NONE) {
            snprintf(buf, sizeof(buf), "%.4f, %.4f  Alt: %.0fm  Speed: %.0f",
                     ll.lat, ll.lon, cam.position.y, cam.move_speed);
        } else {
            snprintf(buf, sizeof(buf), "%.4f, %.4f  Alt: %.0fm  Speed: %.0f  Overlay: %s%s",
                     ll.lat, ll.lon, cam.position.y, cam.move_speed,
                     overlay_name, has_data ? "" : " (no data)");
        }
        draw_text_shadowed(buf, 10, 10, glm::vec4(0.85f, 0.85f, 0.85f, 0.95f), 1.0f, screen_w, screen_h);
    }

    /* Restore GL state */
    glPolygonMode(GL_FRONT_AND_BACK, prev_poly_mode[0]);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

/* Menu text input */
void Hud::menu_text_input(char c) {
    if (m_menu.lat_active) {
        if ((c >= '0' && c <= '9') || c == '.' || c == '-')
            m_menu.lat_input += c;
    } else if (m_menu.lon_active) {
        if ((c >= '0' && c <= '9') || c == '.' || c == '-')
            m_menu.lon_input += c;
    } else if (m_menu.speed_active) {
        if ((c >= '0' && c <= '9') || c == '.')
            m_menu.speed_input += c;
    }
}

void Hud::menu_backspace() {
    if (m_menu.lat_active && !m_menu.lat_input.empty())
        m_menu.lat_input.pop_back();
    else if (m_menu.lon_active && !m_menu.lon_input.empty())
        m_menu.lon_input.pop_back();
    else if (m_menu.speed_active && !m_menu.speed_input.empty())
        m_menu.speed_input.pop_back();
}

void Hud::menu_navigate(int dir) {
    m_menu.lat_active = false;
    m_menu.lon_active = false;
    m_menu.speed_active = false;

    if (m_menu.device_select_node >= 0) {
        // In device selection mode, up/down confirm and move
        m_menu.device_select_node = -1;
        m_menu.editing_node = -1;
    }

    int total = total_menu_fields(Scene{}); // approximate
    m_menu.focused_field = std::max(0, std::min(m_menu.focused_field + dir, total - 1));
}

int Hud::menu_activate(Scene& scene, Camera& cam, GeoProjection& proj) {
    int f = m_menu.focused_field;

    // Deactivate all text inputs first
    m_menu.lat_active = false;
    m_menu.lon_active = false;
    m_menu.speed_active = false;

    // Check if in device selection mode
    if (m_menu.device_select_node >= 0) {
        // Apply device selection
        int ni = m_menu.device_select_node;
        if (ni >= 0 && ni < (int)scene.nodes.size()) {
            auto& hp = HARDWARE_PROFILES[m_menu.device_select_idx];
            auto& nd = scene.nodes[ni];
            nd.info.tx_power_dbm = hp.tx_power_dbm;
            nd.info.antenna_gain_dbi = hp.antenna_gain_dbi;
            nd.info.rx_sensitivity_dbm = hp.rx_sensitivity_dbm;
            nd.info.frequency_mhz = hp.frequency_mhz;
            nd.info.max_range_km = hp.max_range_km;
            scene.build_markers();
            scene.build_spheres();
            LOG_INFO("Node %d device changed to %s (range %.0fkm)",
                     ni, hp.name, hp.max_range_km);
        }
        m_menu.device_select_node = -1;
        m_menu.editing_node = -1;
        return 4; // signal App to kick async viewshed recompute
    }

    // Lat field
    if (f == 0) { m_menu.lat_active = true; return 3; }
    // Lon field
    if (f == 1) { m_menu.lon_active = true; return 3; }
    // Jump button
    if (f == 2) {
        if (!m_menu.lat_input.empty() && !m_menu.lon_input.empty()) {
            double lat = std::atof(m_menu.lat_input.c_str());
            double lon = std::atof(m_menu.lon_input.c_str());
            auto lc = proj.project(lat, lon);
            cam.position.x = lc.x;
            cam.position.z = lc.z;
            LOG_INFO("Jumped to %.4f, %.4f", lat, lon);
        }
        return 3;
    }
    // Speed field
    if (f == 3) { m_menu.speed_active = true; return 3; }
    // Apply speed button
    if (f == 4) {
        if (!m_menu.speed_input.empty()) {
            float spd = std::atof(m_menu.speed_input.c_str());
            if (spd > 0) cam.move_speed = spd;
            LOG_INFO("Camera speed set to %.0f", cam.move_speed);
        }
        return 3;
    }

    // Node list fields
    int node_field_start = 5;
    int node_count = std::min((int)scene.nodes.size(), 8);
    if (f >= node_field_start && f < node_field_start + node_count) {
        int ni = m_menu.scroll_offset + (f - node_field_start);
        if (ni >= 0 && ni < (int)scene.nodes.size()) {
            // Check if clicking on [X] area (rightmost) — we approximate:
            // For simplicity, Enter on a node row toggles edit/device select mode
            if (m_menu.editing_node == ni) {
                // Already editing — enter device select mode
                m_menu.device_select_node = ni;
                // Find current device index
                m_menu.device_select_idx = 0;
                auto& nd = scene.nodes[ni];
                for (int p = 0; p < HARDWARE_PROFILE_COUNT; ++p) {
                    if (std::abs(HARDWARE_PROFILES[p].max_range_km - nd.info.max_range_km) < 0.1f &&
                        std::abs(HARDWARE_PROFILES[p].tx_power_dbm - nd.info.tx_power_dbm) < 0.1f) {
                        m_menu.device_select_idx = p;
                        break;
                    }
                }
            } else {
                m_menu.editing_node = ni;
            }
            return 3;
        }
    }

    // Resume
    int resume_field = node_field_start + node_count;
    if (f == resume_field) return 1;

    // Quit
    if (f == resume_field + 1) return 2;

    return 0;
}

int Hud::total_menu_fields(const Scene& scene) const {
    int node_count = std::min((int)scene.nodes.size(), 8);
    return 5 + node_count + 2; // lat, lon, jump, speed, apply, nodes..., resume, quit
}

bool Hud::is_node_field(int field, const Scene& scene, int& node_idx) const {
    int node_field_start = 5;
    int node_count = std::min((int)scene.nodes.size(), 8);
    if (field >= node_field_start && field < node_field_start + node_count) {
        node_idx = m_menu.scroll_offset + (field - node_field_start);
        return true;
    }
    return false;
}

void Hud::menu_device_left() {
    if (m_menu.device_select_node < 0) return;
    m_menu.device_select_idx = (m_menu.device_select_idx - 1 + HARDWARE_PROFILE_COUNT) % HARDWARE_PROFILE_COUNT;
}

void Hud::menu_device_right() {
    if (m_menu.device_select_node < 0) return;
    m_menu.device_select_idx = (m_menu.device_select_idx + 1) % HARDWARE_PROFILE_COUNT;
}

} // namespace mesh3d
