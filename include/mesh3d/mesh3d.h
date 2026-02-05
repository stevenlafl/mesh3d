#ifndef MESH3D_H
#define MESH3D_H

#include "types.h"

#ifdef _WIN32
  #ifdef MESH3D_EXPORTS
    #define MESH3D_API __declspec(dllexport)
  #else
    #define MESH3D_API __declspec(dllimport)
  #endif
#else
  #define MESH3D_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Lifecycle ─────────────────────────────────────────────────────── */
MESH3D_API int  mesh3d_init(int w, int h, const char* title);
MESH3D_API void mesh3d_shutdown(void);

/* ── Direct data injection ────────────────────────────────────────── */
MESH3D_API int  mesh3d_set_terrain(mesh3d_grid_f32_t grid, mesh3d_bounds_t bounds);
MESH3D_API int  mesh3d_add_node(mesh3d_node_t node);
MESH3D_API int  mesh3d_set_viewshed(int node_idx, mesh3d_grid_u8_t vis, mesh3d_grid_f32_t signal);
MESH3D_API int  mesh3d_set_merged_coverage(mesh3d_grid_u8_t vis, mesh3d_grid_u8_t overlap);

/* ── Control ───────────────────────────────────────────────────────── */
MESH3D_API void mesh3d_set_render_mode(mesh3d_render_mode_t mode);
MESH3D_API void mesh3d_set_overlay_mode(mesh3d_overlay_mode_t mode);
MESH3D_API void mesh3d_toggle_signal_spheres(void);
MESH3D_API void mesh3d_toggle_wireframe(void);
MESH3D_API void mesh3d_rebuild_scene(void);

/* ── Propagation model ────────────────────────────────────────────── */
MESH3D_API void mesh3d_set_propagation_model(mesh3d_prop_model_t model);
MESH3D_API void mesh3d_set_itm_params(mesh3d_itm_params_t params);

/* ── Receiver / display config ───────────────────────────────────── */
MESH3D_API void mesh3d_set_rf_config(mesh3d_rf_config_t config);

/* ── DSM data source ──────────────────────────────────────────────── */
MESH3D_API void mesh3d_set_dsm_dir(const char* dir);

/* Blocking convenience loop */
MESH3D_API void mesh3d_run(void);

/* Or drive manually: */
MESH3D_API int  mesh3d_poll_events(void);   /* returns 0 on quit */
MESH3D_API void mesh3d_frame(float dt);

#ifdef __cplusplus
}
#endif

#endif /* MESH3D_H */
