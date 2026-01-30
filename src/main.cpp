#include "app.h"
#include "util/log.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

int main(int argc, char* argv[]) {
    using namespace mesh3d;

    int width = 1280, height = 720;
    const char* title = "mesh3d — 3D Terrain Viewer";
    const char* texture_path = nullptr;
    double center_lat = 40.3978, center_lon = -105.0750; // Loveland, CO

    /* Simple arg parsing */
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--center") == 0 && i + 1 < argc) {
            const char* val = argv[++i];
            if (std::sscanf(val, "%lf,%lf", &center_lat, &center_lon) != 2) {
                fprintf(stderr, "Invalid --center format, expected LAT,LON\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--texture") == 0 && i + 1 < argc) {
            texture_path = argv[++i];
        } else if (std::strcmp(argv[i], "--debug") == 0) {
            log_set_level(LogLevel::Debug);
        } else if (std::strcmp(argv[i], "--help") == 0) {
            printf("Usage: mesh3d [options]\n"
                   "  --center LAT,LON  Starting center (default: 40.3978,-105.075 Loveland CO)\n"
                   "  --texture PATH    Load satellite texture from file\n"
                   "  --width W         Window width (default 1280)\n"
                   "  --height H        Window height (default 720)\n"
                   "  --debug           Enable debug logging\n"
                   "\nControls:\n"
                   "  WASD        Move camera\n"
                   "  Q/E         Move down/up\n"
                   "  Right-click Hold to look around\n"
                   "  Scroll      Zoom FOV\n"
                   "  Shift       Sprint (4x speed)\n"
                   "  Tab         Toggle terrain/flat mode\n"
                   "  1           Viewshed overlay\n"
                   "  2           Signal strength overlay\n"
                   "  3           Cycle imagery (satellite/street/none)\n"
                   "  T           Toggle signal spheres\n"
                   "  F           Toggle wireframe\n"
                   "  Escape      Release mouse / quit\n");
            return 0;
        }
    }

    auto& a = app();
    if (!a.init(width, height, title)) {
        LOG_ERROR("Failed to initialize");
        return 1;
    }

    /* HGT streaming mode (always active) */
    if (!a.init_hgt_mode(center_lat, center_lon)) {
        LOG_ERROR("Failed to initialize HGT mode");
        a.shutdown();
        return 1;
    }

    /* Load manual texture override if specified */
    if (texture_path) {
        if (a.scene.satellite_tex.load(texture_path)) {
            LOG_INFO("Loaded texture: %s", texture_path);
        } else {
            LOG_WARN("Failed to load texture: %s", texture_path);
        }
    }

    a.run();
    a.shutdown();
    return 0;
}
