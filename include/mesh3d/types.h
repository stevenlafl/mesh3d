#ifndef MESH3D_TYPES_H
#define MESH3D_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double min_lat, max_lat;
    double min_lon, max_lon;
} mesh3d_bounds_t;

typedef struct {
    int rows;
    int cols;
    float* data;  /* row-major */
} mesh3d_grid_f32_t;

typedef struct {
    int rows;
    int cols;
    uint8_t* data;  /* row-major */
} mesh3d_grid_u8_t;

typedef struct {
    int id;
    char name[128];
    double lat, lon, alt;
    float antenna_height_m;
    float max_range_km;
    int role;  /* 0=backbone, 1=relay, 2=leaf */
    /* hardware profile */
    float tx_power_dbm;
    float antenna_gain_dbi;
    float rx_sensitivity_dbm;
    float frequency_mhz;
} mesh3d_node_t;

typedef enum {
    MESH3D_MODE_TERRAIN  = 0, /* Mode A: heightmap terrain */
    MESH3D_MODE_FLAT     = 1  /* Mode B: flat grid plane */
} mesh3d_render_mode_t;

typedef enum {
    MESH3D_OVERLAY_NONE     = 0,
    MESH3D_OVERLAY_VIEWSHED = 1, /* binary visible/not */
    MESH3D_OVERLAY_SIGNAL   = 2  /* signal strength heatmap */
} mesh3d_overlay_mode_t;

#ifdef __cplusplus
}
#endif

#endif /* MESH3D_TYPES_H */
