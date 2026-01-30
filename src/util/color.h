#pragma once
#include <glm/glm.hpp>
#include <algorithm>

namespace mesh3d {

/* Signal strength (dBm) to RGB color.
   -80 dBm = strong green, -105 dBm = yellow, -130 dBm = red. */
inline glm::vec3 signal_to_color(float dbm) {
    float t = std::clamp((dbm - (-130.0f)) / ((-80.0f) - (-130.0f)), 0.0f, 1.0f);
    /* green -> yellow -> red */
    float r = (t < 0.5f) ? 1.0f : 1.0f - 2.0f * (t - 0.5f);
    float g = (t < 0.5f) ? 2.0f * t : 1.0f;
    // refine: strong = green, medium = yellow, weak = red
    r = 1.0f - t;         // 1 at weak, 0 at strong
    g = t;                // 0 at weak, 1 at strong
    if (t > 0.5f) { r = 2.0f * (1.0f - t); g = 1.0f; }
    else          { r = 1.0f; g = 2.0f * t; }
    return {r, g, 0.0f};
}

/* Node role to color */
inline glm::vec3 role_color(int role) {
    switch (role) {
        case 0: return {0.2f, 0.4f, 1.0f}; // backbone = blue
        case 1: return {0.2f, 0.9f, 0.3f}; // relay = green
        case 2: return {1.0f, 0.6f, 0.1f}; // leaf = orange
        default: return {0.8f, 0.8f, 0.8f};
    }
}

} // namespace mesh3d
