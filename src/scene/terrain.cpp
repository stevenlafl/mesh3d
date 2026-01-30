#include "scene/terrain.h"
#include "util/math_util.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace mesh3d {

/* 11 floats per vertex: pos(3) normal(3) uv(2) viewshed(1) signal(1) = 10
   Corrected: pos(3)+normal(3)+uv(2)+viewshed(1)+signal(1) = 10 */
static constexpr int VERT_FLOATS = 10;

static glm::vec3 calc_normal(const float* elev, int r, int c, int rows, int cols,
                              float dx, float dz, float yscale) {
    auto h = [&](int rr, int cc) -> float {
        rr = std::clamp(rr, 0, rows - 1);
        cc = std::clamp(cc, 0, cols - 1);
        return elev[rr * cols + cc] * yscale;
    };
    float dhdx = (h(r, c+1) - h(r, c-1)) / (2.0f * dx);
    float dhdz = (h(r+1, c) - h(r-1, c)) / (2.0f * dz);
    return glm::normalize(glm::vec3(-dhdx, 1.0f, -dhdz));
}

Mesh build_terrain_mesh(const TerrainBuildData& data, const GeoProjection& proj) {
    int rows = data.rows;
    int cols = data.cols;
    float w = proj.width_m(data.bounds);
    float h = proj.height_m(data.bounds);
    float dx = w / (cols - 1);
    float dz = h / (rows - 1);
    float yscale = data.elevation_scale;

    /* Vertices */
    std::vector<float> verts(rows * cols * VERT_FLOATS);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int vi = (r * cols + c) * VERT_FLOATS;
            float u = static_cast<float>(c) / (cols - 1);
            float v = static_cast<float>(r) / (rows - 1);

            /* Position: X = east, Y = up, Z = south */
            float x = -w * 0.5f + c * dx;
            float z = -h * 0.5f + r * dz;
            float y = data.elevation[r * cols + c] * yscale;

            glm::vec3 n = calc_normal(data.elevation, r, c, rows, cols, dx, dz, yscale);

            float vis = 0.0f;
            float sig = -999.0f;
            if (data.viewshed) vis = data.viewshed[r * cols + c] ? 1.0f : 0.0f;
            if (data.signal)   sig = data.signal[r * cols + c];

            verts[vi + 0] = x;
            verts[vi + 1] = y;
            verts[vi + 2] = z;
            verts[vi + 3] = n.x;
            verts[vi + 4] = n.y;
            verts[vi + 5] = n.z;
            verts[vi + 6] = u;
            verts[vi + 7] = v;
            verts[vi + 8] = vis;
            verts[vi + 9] = sig;
        }
    }

    /* Indices (two triangles per quad) */
    std::vector<uint32_t> indices;
    indices.reserve((rows - 1) * (cols - 1) * 6);
    for (int r = 0; r < rows - 1; ++r) {
        for (int c = 0; c < cols - 1; ++c) {
            uint32_t tl = r * cols + c;
            uint32_t tr = tl + 1;
            uint32_t bl = (r + 1) * cols + c;
            uint32_t br = bl + 1;
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    Mesh mesh;
    GLsizei stride = VERT_FLOATS * sizeof(float);
    std::vector<Mesh::Attrib> attribs = {
        {0, 3, GL_FLOAT, stride, 0},                        // position
        {1, 3, GL_FLOAT, stride, 3 * sizeof(float)},        // normal
        {2, 2, GL_FLOAT, stride, 6 * sizeof(float)},        // uv
        {3, 1, GL_FLOAT, stride, 8 * sizeof(float)},        // viewshed
        {4, 1, GL_FLOAT, stride, 9 * sizeof(float)},        // signal_dbm
    };
    mesh.upload(verts.data(), verts.size() * sizeof(float), attribs,
                indices.data(), indices.size() * sizeof(uint32_t));
    return mesh;
}

Mesh build_flat_mesh(int rows, int cols, float width_m, float height_m) {
    float dx = width_m / (cols - 1);
    float dz = height_m / (rows - 1);

    /* Simple flat grid: pos(3) + uv(2) = 5 floats */
    std::vector<float> verts(rows * cols * 5);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int vi = (r * cols + c) * 5;
            verts[vi + 0] = -width_m * 0.5f + c * dx;
            verts[vi + 1] = 0.0f;
            verts[vi + 2] = -height_m * 0.5f + r * dz;
            verts[vi + 3] = static_cast<float>(c) / (cols - 1);
            verts[vi + 4] = static_cast<float>(r) / (rows - 1);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve((rows - 1) * (cols - 1) * 6);
    for (int r = 0; r < rows - 1; ++r) {
        for (int c = 0; c < cols - 1; ++c) {
            uint32_t tl = r * cols + c;
            uint32_t tr = tl + 1;
            uint32_t bl = (r + 1) * cols + c;
            uint32_t br = bl + 1;
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    Mesh mesh;
    GLsizei stride = 5 * sizeof(float);
    std::vector<Mesh::Attrib> attribs = {
        {0, 3, GL_FLOAT, stride, 0},
        {1, 2, GL_FLOAT, stride, 3 * sizeof(float)},
    };
    mesh.upload(verts.data(), verts.size() * sizeof(float), attribs,
                indices.data(), indices.size() * sizeof(uint32_t));
    return mesh;
}

void generate_synthetic_terrain(std::vector<float>& out, int rows, int cols) {
    out.resize(rows * cols);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            float u = static_cast<float>(c) / (cols - 1);
            float v = static_cast<float>(r) / (rows - 1);
            /* Rolling hills */
            float h = 50.0f * std::sin(u * 6.28f * 2.0f) * std::cos(v * 6.28f * 1.5f)
                    + 30.0f * std::sin(u * 6.28f * 5.0f + 1.0f)
                    + 20.0f * std::cos(v * 6.28f * 3.0f + 2.0f)
                    + 200.0f; // base elevation
            out[r * cols + c] = h;
        }
    }
}

} // namespace mesh3d
