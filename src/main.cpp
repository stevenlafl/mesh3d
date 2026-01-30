#include "app.h"
#include "util/log.h"
#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    using namespace mesh3d;

    int width = 1280, height = 720;
    const char* title = "mesh3d — 3D Viewshed Visualizer";
    const char* db_conninfo = nullptr;
    const char* texture_path = nullptr;
    int project_id = -1;

    /* Simple arg parsing */
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_conninfo = argv[++i];
        } else if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            project_id = std::atoi(argv[++i]);
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
                   "  --db CONNINFO     PostgreSQL connection string\n"
                   "  --project ID      Project ID to load\n"
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

    /* Optionally connect to DB and load project */
    if (db_conninfo) {
        if (!a.connect_db(db_conninfo)) {
            LOG_WARN("DB connection failed, running with demo data");
        } else if (project_id > 0) {
            if (!a.load_project(project_id)) {
                LOG_WARN("Failed to load project %d, running with demo data", project_id);
            }
        }
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
