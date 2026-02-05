# mesh3d

A standalone C++17 application that renders Meshtastic radio viewshed data in 3D. It reads terrain elevation from SRTM HGT files (with optional satellite tile streaming), places radio node markers, computes line-of-sight viewsheds with ITM propagation modeling, and renders an interactive 3D scene.

Features:

- Heightmap terrain with viewshed/signal heatmap overlays
- Satellite and street map tile streaming
- Opaque icosphere markers at each radio node (color-coded by role)
- Translucent signal-range spheres with Fresnel edge effects
- Switchable terrain / flat-plane mode
- Free-fly camera (WASD + mouse)

Built with SDL2 + OpenGL 3.3 core profile. Also exposes a C ABI shared library (`libmesh3d.so`) for embedding in other programs.

## Build

### Docker (recommended)

No host dependencies needed beyond Docker.

```sh
docker compose build build
docker compose run --rm build
```

### Native

Install dependencies (Ubuntu/Debian):

```sh
sudo apt install build-essential cmake pkg-config libsdl2-dev libgl-dev libglm-dev libcurl4-openssl-dev zlib1g-dev
```

Build:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Output: `build/mesh3d` (executable) and `build/libmesh3d.so` (shared library).

## Run

```sh
# Default (streams SRTM terrain for Loveland, CO area):
./build/mesh3d

# Custom center location:
./build/mesh3d --center 38.5,-105.5

# With a satellite texture overlay:
./build/mesh3d --texture path/to/satellite.png

# Other flags:
./build/mesh3d --width 1920 --height 1080 --debug
```

Requires a display server and OpenGL 3.3 support on the host. No pre-downloaded data is needed — everything is fetched on the fly.

## Streaming and Caching

The application dynamically streams all terrain and imagery data based on the camera position. Nothing needs to be downloaded ahead of time.

**Elevation:** SRTM HGT tiles (1° x 1°) are fetched from AWS S3 (`elevation-tiles-prod/skadi`), decompressed from gzip, and cached to `~/.cache/mesh3d/hgt/`. As the camera moves, nearby tiles are loaded automatically — up to 4 at a time based on proximity to tile edges.

**Imagery:** Satellite tiles (Esri World Imagery) and street map tiles (OpenStreetMap) use standard slippy map URLs at zoom level 13. Downloaded tiles are cached to `~/.cache/mesh3d/tiles/` and composited to match the elevation tile bounds.

**Pipeline:** All network fetches happen on a background worker thread. Completed tiles are drained on the main thread with a 4ms per-frame budget to avoid stutter. An LRU cache (128 tiles) manages GPU memory, evicting the least recently used tiles when full.

## Controls

| Key | Action |
|-----|--------|
| WASD | Move camera |
| Q / E | Move down / up |
| Right-click | Hold to mouselook |
| Scroll | FOV zoom |
| Shift | Sprint (4x speed) |
| Tab | Toggle terrain / flat mode |
| 1 | Viewshed binary overlay |
| 2 | Signal strength heatmap overlay |
| 3 | Cycle imagery (satellite / street / none) |
| T | Toggle signal spheres |
| F | Toggle wireframe |
| Escape | Release mouse / quit |

## C API

The shared library exposes a C ABI for embedding in other applications:

```c
#include <mesh3d/mesh3d.h>

// Initialize window and OpenGL context
mesh3d_init(1280, 720, "My Viewer");

// Inject terrain and node data directly
mesh3d_set_terrain(grid, bounds);
mesh3d_add_node(node);
mesh3d_set_viewshed(0, visibility, signal);
mesh3d_rebuild_scene();

// Run the render loop
mesh3d_run();

mesh3d_shutdown();
```

See [`include/mesh3d/mesh3d.h`](include/mesh3d/mesh3d.h) and [`include/mesh3d/types.h`](include/mesh3d/types.h) for the full API reference.

## License

MIT License. See [LICENSE](LICENSE) for details.
