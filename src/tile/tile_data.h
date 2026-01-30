#pragma once
#include "tile/tile_coord.h"
#include "render/mesh.h"
#include "render/texture.h"
#include <mesh3d/types.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace mesh3d {

/* CPU-side raw tile data (elevation, imagery, overlays) */
struct TileData {
    TileCoord coord;
    mesh3d_bounds_t bounds;

    /* Elevation grid */
    std::vector<float> elevation;
    int elev_rows = 0, elev_cols = 0;

    /* Imagery (RGBA) */
    std::vector<uint8_t> imagery;
    int img_width = 0, img_height = 0;

    /* Overlay data */
    std::vector<uint8_t> viewshed;
    std::vector<float> signal;
};

/* GPU-side tile ready for rendering.
   Also retains CPU-side elevation for runtime queries (raycast, etc.) */
struct TileRenderable {
    TileCoord coord;
    mesh3d_bounds_t bounds;
    Mesh mesh;
    Texture texture;
    glm::mat4 model{1.0f};

    /* CPU-side elevation retained for sampling */
    std::vector<float> elevation;
    int elev_rows = 0, elev_cols = 0;

    /* CPU-side overlay data (populated by viewshed computation) */
    std::vector<uint8_t> viewshed;
    std::vector<float> signal;

    /* GPU overlay textures — viewshed (R8UI) and signal (R32F).
       When valid, the terrain shader samples these instead of vertex attributes,
       avoiding a full mesh rebuild on viewshed updates. */
    GLuint overlay_vis_tex = 0;   // R8 (normalized, not integer)
    GLuint overlay_sig_tex = 0;   // R32F
    bool   overlay_tex_valid = false;

    void destroy_overlay_textures() {
        if (overlay_vis_tex) { glDeleteTextures(1, &overlay_vis_tex); overlay_vis_tex = 0; }
        if (overlay_sig_tex) { glDeleteTextures(1, &overlay_sig_tex); overlay_sig_tex = 0; }
        overlay_tex_valid = false;
    }

    void upload_overlay_textures(const uint8_t* vis, const float* sig, int rows, int cols) {
        if (!overlay_vis_tex) {
            glGenTextures(1, &overlay_vis_tex);
            glBindTexture(GL_TEXTURE_2D, overlay_vis_tex);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8, cols, rows);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        if (!overlay_sig_tex) {
            glGenTextures(1, &overlay_sig_tex);
            glBindTexture(GL_TEXTURE_2D, overlay_sig_tex);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, cols, rows);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        /* Scale viewshed 0/1 → 0/255 for GL_R8 normalized (1/255 ≈ 0.004, not 1.0) */
        int total = rows * cols;
        std::vector<uint8_t> scaled_vis(total);
        for (int i = 0; i < total; ++i)
            scaled_vis[i] = vis[i] ? 255 : 0;

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, overlay_vis_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cols, rows,
                        GL_RED, GL_UNSIGNED_BYTE, scaled_vis.data());
        glBindTexture(GL_TEXTURE_2D, overlay_sig_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cols, rows,
                        GL_RED, GL_FLOAT, sig);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);

        overlay_tex_valid = true;
    }
};

} // namespace mesh3d
