cmake_minimum_required(VERSION 3.16)
project(mesh3d VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────────────
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)

# GLM — prefer system, fall back to bundled
find_package(glm QUIET)
if(NOT glm_FOUND)
    message(STATUS "System GLM not found, using bundled third_party/glm")
    set(GLM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/glm")
else()
    get_target_property(GLM_INCLUDE_DIR glm::glm INTERFACE_INCLUDE_DIRECTORIES)
endif()

# ── GLAD (bundled) ────────────────────────────────────────────────────
add_library(glad STATIC third_party/glad/src/glad.c)
target_include_directories(glad PUBLIC third_party/glad/include)
set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── stb (header-only) ────────────────────────────────────────────────
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE third_party/stb)

# ── Library sources ───────────────────────────────────────────────────
set(LIB_SOURCES
    src/app.cpp
    src/scene/scene.cpp
    src/scene/terrain.cpp
    src/scene/node_marker.cpp
    src/scene/signal_sphere.cpp
    src/render/renderer.cpp
    src/render/shader.cpp
    src/render/mesh.cpp
    src/render/texture.cpp
    src/camera/camera.cpp
    src/camera/input.cpp
    src/cabi/cabi.cpp
    src/ui/hud.cpp
    src/analysis/viewshed.cpp
    src/util/log.cpp
    src/tile/tile_provider.cpp
    src/tile/single_tile_provider.cpp
    src/tile/url_tile_provider.cpp
    src/tile/tile_cache.cpp
    src/tile/tile_terrain_builder.cpp
    src/tile/tile_selector.cpp
    src/tile/tile_manager.cpp
    src/tile/disk_cache.cpp
    src/tile/hgt_provider.cpp
)

add_library(mesh3d_lib SHARED ${LIB_SOURCES})
target_include_directories(mesh3d_lib
    PUBLIC  include
    PRIVATE src ${GLM_INCLUDE_DIR}
)
target_link_libraries(mesh3d_lib
    PUBLIC  glad stb OpenGL::GL CURL::libcurl ZLIB::ZLIB
)
target_compile_definitions(mesh3d_lib PRIVATE MESH3D_EXPORTS)

# Handle SDL2 target differences across distros
if(TARGET SDL2::SDL2)
    target_link_libraries(mesh3d_lib PUBLIC SDL2::SDL2)
else()
    target_include_directories(mesh3d_lib PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(mesh3d_lib PUBLIC ${SDL2_LIBRARIES})
endif()

set_target_properties(mesh3d_lib PROPERTIES
    OUTPUT_NAME mesh3d
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

# ── Executable ────────────────────────────────────────────────────────
add_executable(mesh3d_app src/main.cpp)
target_include_directories(mesh3d_app PRIVATE src ${GLM_INCLUDE_DIR})
target_link_libraries(mesh3d_app PRIVATE mesh3d_lib)
set_target_properties(mesh3d_app PROPERTIES
    OUTPUT_NAME mesh3d
    INSTALL_RPATH "$ORIGIN"
    BUILD_RPATH "$ORIGIN"
)
