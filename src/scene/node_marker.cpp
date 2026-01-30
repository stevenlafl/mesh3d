#include "scene/node_marker.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <cmath>
#include <cstdint>

namespace mesh3d {

/* Icosphere generation */
struct IcoBuilder {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t>  indices;

    using EdgeKey = uint64_t;
    std::unordered_map<EdgeKey, uint32_t> midpoint_cache;

    static EdgeKey edge_key(uint32_t a, uint32_t b) {
        if (a > b) std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | b;
    }

    uint32_t add_vertex(glm::vec3 p) {
        positions.push_back(glm::normalize(p));
        return static_cast<uint32_t>(positions.size() - 1);
    }

    uint32_t get_midpoint(uint32_t a, uint32_t b) {
        EdgeKey key = edge_key(a, b);
        auto it = midpoint_cache.find(key);
        if (it != midpoint_cache.end()) return it->second;
        glm::vec3 mid = (positions[a] + positions[b]) * 0.5f;
        uint32_t idx = add_vertex(mid);
        midpoint_cache[key] = idx;
        return idx;
    }

    void build_icosahedron() {
        float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
        add_vertex({-1, t, 0}); add_vertex({1, t, 0});
        add_vertex({-1, -t, 0}); add_vertex({1, -t, 0});
        add_vertex({0, -1, t}); add_vertex({0, 1, t});
        add_vertex({0, -1, -t}); add_vertex({0, 1, -t});
        add_vertex({t, 0, -1}); add_vertex({t, 0, 1});
        add_vertex({-t, 0, -1}); add_vertex({-t, 0, 1});

        uint32_t faces[] = {
            0,11,5,  0,5,1,   0,1,7,   0,7,10,  0,10,11,
            1,5,9,   5,11,4,  11,10,2,  10,7,6,  7,1,8,
            3,9,4,   3,4,2,   3,2,6,   3,6,8,   3,8,9,
            4,9,5,   2,4,11,  6,2,10,  8,6,7,   9,8,1,
        };
        indices.assign(std::begin(faces), std::end(faces));
    }

    void subdivide() {
        std::vector<uint32_t> new_indices;
        new_indices.reserve(indices.size() * 4);
        for (size_t i = 0; i < indices.size(); i += 3) {
            uint32_t a = indices[i], b = indices[i+1], c = indices[i+2];
            uint32_t ab = get_midpoint(a, b);
            uint32_t bc = get_midpoint(b, c);
            uint32_t ca = get_midpoint(c, a);
            new_indices.push_back(a);  new_indices.push_back(ab); new_indices.push_back(ca);
            new_indices.push_back(b);  new_indices.push_back(bc); new_indices.push_back(ab);
            new_indices.push_back(c);  new_indices.push_back(ca); new_indices.push_back(bc);
            new_indices.push_back(ab); new_indices.push_back(bc); new_indices.push_back(ca);
        }
        indices = std::move(new_indices);
        midpoint_cache.clear();
    }
};

Mesh build_icosphere(int subdivisions) {
    IcoBuilder b;
    b.build_icosahedron();
    for (int i = 0; i < subdivisions; ++i) b.subdivide();

    /* Build vertex data: pos(3) + normal(3) = 6 floats (normal = pos for unit sphere) */
    std::vector<float> verts(b.positions.size() * 6);
    for (size_t i = 0; i < b.positions.size(); ++i) {
        glm::vec3 p = b.positions[i];
        verts[i*6+0] = p.x; verts[i*6+1] = p.y; verts[i*6+2] = p.z;
        verts[i*6+3] = p.x; verts[i*6+4] = p.y; verts[i*6+5] = p.z; // normal = pos
    }

    Mesh mesh;
    GLsizei stride = 6 * sizeof(float);
    std::vector<Mesh::Attrib> attribs = {
        {0, 3, GL_FLOAT, stride, 0},
        {1, 3, GL_FLOAT, stride, 3 * sizeof(float)},
    };
    mesh.upload(verts.data(), verts.size() * sizeof(float), attribs,
                b.indices.data(), b.indices.size() * sizeof(uint32_t));
    return mesh;
}

} // namespace mesh3d
