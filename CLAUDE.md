# mesh3d — AI Agent Guide

## What This Is

A standalone C++17 application that renders Meshtastic radio viewshed data in 3D. It reads terrain elevation from SRTM HGT files, and accepts node positions and signal coverage data via the C API, then renders an interactive 3D scene with:

- Heightmap terrain with viewshed/signal heatmap overlays
- Opaque icosphere markers at each radio node (color-coded by role)
- Translucent signal-range spheres with Fresnel edge effects
- A switchable flat-plane mode
- Free-fly camera (WASD + mouse)

Built with SDL2 + OpenGL 3.3 core profile. Exposes a C ABI shared library (`libmesh3d.so`) for embedding in other programs.

---

## Build

All building happens inside Docker. No host dependencies needed beyond Docker.

```sh
# First time: builds the image
docker compose build build

# Compile (runs cmake configure + build inside container)
docker compose run --rm build
```

Output: `build/mesh3d` (executable), `build/libmesh3d.so` (shared library).

The build container (Ubuntu 24.04) has: build-essential, cmake, libsdl2-dev, libgl-dev, libglm-dev.

---

## Run

```sh
# Demo mode (synthetic terrain):
./build/mesh3d

# Other flags:
#   --width W --height H --debug --help
```

Note: requires a display server and OpenGL 3.3 support on the host (the app creates a window).

---

## Project Layout

```
mesh3d/
├── CMakeLists.txt                     # Build: targets mesh3d_lib (shared) + mesh3d_app (exe)
├── docker-compose.yml                 # Services: build (compile container)
├── docker/
│   └── Dockerfile.build               # Build image: Ubuntu 24.04 + dev packages
├── include/mesh3d/
│   ├── mesh3d.h                       # PUBLIC C ABI (lifecycle, data injection, control, loop)
│   └── types.h                        # PUBLIC C structs/enums (bounds, grids, nodes, modes)
├── src/
│   ├── main.cpp                       # Entry point, CLI arg parsing
│   ├── app.h / app.cpp                # App singleton: SDL/GL init, main loop, demo scene gen
│   ├── cabi/cabi.cpp                  # C ABI wrappers -> app() singleton calls
│   ├── camera/
│   │   ├── camera.h / camera.cpp      # 6DOF camera: position, yaw/pitch, view/proj matrices
│   │   └── input.h / input.cpp        # SDL2 events -> movement, toggles, mouse capture
│   ├── render/
│   │   ├── renderer.h / renderer.cpp  # Orchestrates opaque + transparent passes, owns 4 shaders
│   │   ├── shader.h / shader.cpp      # GLSL compile/link, uniform setters
│   │   ├── mesh.h / mesh.cpp          # VAO/VBO/EBO RAII wrapper with attribute layout
│   │   └── texture.h / texture.cpp    # stb_image loader, GL texture object
│   ├── scene/
│   │   ├── scene.h / scene.cpp        # Scene struct: all meshes + data + rebuild_all()
│   │   ├── terrain.h / terrain.cpp    # Grid mesh builders (heightmap + flat), synthetic generator
│   │   ├── node_marker.h / .cpp       # Icosphere generation (subdivision surfaces)
│   │   └── signal_sphere.h / .cpp     # Coverage sphere = icosphere(subdivisions=3)
│   └── util/
│       ├── math_util.h                # GeoProjection: lat/lon -> local XZ meters
│       ├── color.h                    # signal_to_color (dBm->RGB), role_color (role->RGB)
│       └── log.h / log.cpp            # Printf logger: LOG_DEBUG/INFO/WARN/ERROR macros
├── shaders/
│   ├── terrain.vert / terrain.frag    # Height-colored terrain, viewshed/signal overlays, lighting
│   ├── flat.vert / flat.frag          # Flat grid with procedural grid-line pattern
│   ├── marker.vert / marker.frag      # Solid lit icospheres with uniform color
│   └── sphere.vert / sphere.frag      # Transparent spheres with Fresnel alpha
└── third_party/
    ├── glad/                          # Generated OpenGL 3.3 core loader (glad.c + glad.h)
    ├── stb/stb_image.h                # Single-header image loading
    └── glm/                           # Header-only math (fallback; system GLM preferred)
```

---

## Architecture

### Singleton & Data Flow

```
App (singleton: static g_app in app.cpp, accessed via app())
 ├─ Scene        ← data holder (elevation[], nodes[], viewshed[], meshes, textures)
 ├─ Camera       ← position, orientation, projection
 ├─ Renderer     ← owns 4 Shader objects, drives GL draw calls
 └─ InputHandler ← SDL event processing, held-key state, toggle flags
```

Data flows in one direction:

```
[C API injection] or [Synthetic demo]
        ↓
  Scene raw data (elevation, nodes, viewshed arrays)
        ↓
  scene.rebuild_all()  →  GPU meshes (Mesh objects with VAO/VBO/EBO)
        ↓
  Renderer::render()   →  opaque pass (terrain/flat + markers)
                       →  transparent pass (spheres, sorted back-to-front)
        ↓
  SDL_GL_SwapWindow
```

### Rendering Pipeline (per frame)

1. **Clear** to `(0.12, 0.14, 0.18)`.
2. **Opaque pass** — depth write ON, blend OFF:
   - Terrain mesh (Mode A) or flat grid (Mode B), with overlay mode uniform.
   - Node marker icospheres, one draw call each, color by role.
3. **Transparent pass** — depth write OFF, alpha blend `SRC_ALPHA/ONE_MINUS_SRC_ALPHA`:
   - Signal spheres sorted back-to-front by camera distance.
   - Fresnel alpha: `baseAlpha(0.18) + pow(1 - |dot(N,V)|, 2) * 0.4`, clamped to 0.7.

### Keyboard Controls

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
| T | Toggle signal spheres |
| F | Toggle wireframe |
| Escape | Release mouse / quit |

---

## Code Conventions

### Naming

| Element | Convention | Examples |
|---------|-----------|----------|
| Namespace | `mesh3d` | |
| Classes | PascalCase | `App`, `Camera`, `Renderer`, `Shader`, `Mesh`, `Texture` |
| Structs | PascalCase | `Scene`, `NodeData`, `TerrainBuildData`, `GeoProjection` |
| Member vars | `m_` prefix | `m_window`, `m_program`, `m_vao`, `m_quit` |
| Free functions | snake_case | `build_terrain_mesh()`, `build_icosphere()`, `role_color()` |
| C ABI functions | `mesh3d_` prefix | `mesh3d_init()`, `mesh3d_set_terrain()` |
| C types | `mesh3d_` prefix + `_t` | `mesh3d_bounds_t`, `mesh3d_node_t` |
| Shader uniforms | `u` prefix | `uModel`, `uView`, `uProj`, `uColor` |
| Shader attributes | `a` prefix | `aPos`, `aNormal`, `aUV` |
| Shader varyings | `v` prefix | `vWorldPos`, `vNormal`, `vUV` |
| Files | snake_case | `node_marker.cpp`, `signal_sphere.cpp`, `math_util.h` |
| Header guards | `#pragma once` (internal), `#ifndef` (public C) | |

### Include Paths

Internal `src/` headers use quoted paths relative to `src/`:
```cpp
#include "render/shader.h"
#include "util/log.h"
#include "scene/scene.h"
```

Public headers use angle brackets:
```cpp
#include <mesh3d/mesh3d.h>   // from include/mesh3d/
#include <mesh3d/types.h>
```

Third-party:
```cpp
#include <glad/glad.h>       // third_party/glad/include/
#include <glm/glm.hpp>       // system or third_party/glm/
#include <stb_image.h>       // third_party/stb/
#include <SDL2/SDL.h>        // system
```

### RAII / Ownership

`Mesh`, `Shader`, `Texture` are **move-only** types. They hold OpenGL handles (VAO/VBO/EBO, program, texture ID) and delete them in destructors. Copy constructors are deleted; move constructors null out the source.

### Adding a New Shader

1. Create `shaders/myshader.vert` and `shaders/myshader.frag`.
2. Add a `Shader myshader;` member to `Renderer` (`src/render/renderer.h`).
3. Load it in `Renderer::init()` (`src/render/renderer.cpp`).
4. Use it in `opaque_pass()` or `transparent_pass()`.

### Adding a New Scene Object Type

1. Create `src/scene/my_object.h` / `.cpp` with a build function returning `Mesh`.
2. Add mesh/model/data vectors to `Scene` (`src/scene/scene.h`).
3. Add a `build_my_objects()` method to `Scene`, call from `rebuild_all()`.
4. Add render calls in `Renderer::opaque_pass()` or `transparent_pass()`.
5. Add the `.cpp` to `LIB_SOURCES` in `CMakeLists.txt`.

### Vertex Attribute Layouts

| Mesh Type | Stride | Attributes |
|-----------|--------|------------|
| Terrain | 10 floats (40B) | 0:pos(3), 1:normal(3), 2:uv(2), 3:viewshed(1), 4:signal_dbm(1) |
| Flat plane | 5 floats (20B) | 0:pos(3), 1:uv(2) |
| Marker/Sphere | 6 floats (24B) | 0:pos(3), 1:normal(3) |

---

## C ABI Quick Reference

All functions are `extern "C"` with `MESH3D_API` visibility. Return `int` (1=ok, 0=error) for boolean results.

```c
// Lifecycle
mesh3d_init(w, h, title)        // SDL window + GL context + shader load
mesh3d_shutdown()

// Direct data injection
mesh3d_set_terrain(grid, bounds)
mesh3d_add_node(node)                          // returns node index
mesh3d_set_viewshed(node_idx, vis, signal)
mesh3d_set_merged_coverage(vis, overlap)

// Control
mesh3d_set_render_mode(mode)    // MESH3D_MODE_TERRAIN or MESH3D_MODE_FLAT
mesh3d_set_overlay_mode(mode)   // NONE / VIEWSHED / SIGNAL
mesh3d_toggle_signal_spheres()
mesh3d_toggle_wireframe()
mesh3d_rebuild_scene()

// Main loop (pick one)
mesh3d_run()                    // blocking loop (easiest)
// or manual:
mesh3d_poll_events()            // returns 0 on quit
mesh3d_frame(dt)
```

---

## Coordinate System

- **World origin** is the geographic center of the project bounds.
- **X** = east (longitude), **Z** = south (latitude, inverted so north = -Z), **Y** = up (elevation in meters).
- `GeoProjection` in `src/util/math_util.h` converts lat/lon to local XZ meters using simple equirectangular approximation.

---

## Key Files for Common Tasks

| Task | Files to Edit |
|------|---------------|
| Change terrain appearance | `shaders/terrain.frag`, `src/scene/terrain.cpp` |
| Change sphere transparency | `shaders/sphere.frag` (Fresnel params), `src/render/renderer.cpp` (`uBaseAlpha`) |
| Add new overlay mode | `include/mesh3d/types.h` (enum), `shaders/terrain.frag`, `src/render/renderer.cpp` |
| Modify camera behavior | `src/camera/camera.h` (speeds/fov), `src/camera/input.cpp` (key bindings) |
| Add new keyboard shortcut | `src/camera/input.h` (flag), `src/camera/input.cpp` (key handler), `src/app.cpp` (`handle_toggles`) |
| Add new C ABI function | `include/mesh3d/mesh3d.h` (declaration), `src/cabi/cabi.cpp` (impl), `src/app.h` (method) |
| Add new source file | Create file, add to `LIB_SOURCES` in `CMakeLists.txt` |
