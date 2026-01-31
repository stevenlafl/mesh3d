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
    float cable_loss_db;       /* TX cable/connector loss */
    float bandwidth_khz;       /* channel bandwidth */
    int   spreading_factor;    /* LoRa SF (7-12) */
} mesh3d_node_t;

typedef struct {
    int   climate;              /* ITU climate zone (1-7) */
    float ground_dielectric;    /* relative permittivity */
    float ground_conductivity;  /* S/m */
    int   polarization;         /* 0=horizontal, 1=vertical */
    float situation_pct;        /* situation % (1-99), default 50 */
    float time_pct;             /* time % (1-99), default 50 */
    float refractivity;         /* N_0, N-Units, default 301 */
    float location_pct;         /* location % (1-99), default 50 */
    int   mdvar;                /* mode of variability (0-13), default 12 */
} mesh3d_itm_params_t;

typedef struct {
    float rx_sensitivity_dbm;   /* -130.0 */
    float rx_height_agl_m;      /* 1.0 */
    float rx_antenna_gain_dbi;  /* 2.0 */
    float rx_cable_loss_db;     /* 2.0 */
    float display_min_dbm;      /* -130.0 (bottom of signal color scale) */
    float display_max_dbm;      /* -80.0  (top of signal color scale) */
} mesh3d_rf_config_t;

typedef enum {
    MESH3D_PROP_FSPL    = 0,  /* free-space path loss (current) */
    MESH3D_PROP_ITM     = 1,  /* Longley-Rice ITM */
    MESH3D_PROP_FRESNEL = 2   /* Fresnel-Kirchhoff */
} mesh3d_prop_model_t;

typedef enum {
    MESH3D_MODE_TERRAIN  = 0, /* Mode A: heightmap terrain */
    MESH3D_MODE_FLAT     = 1  /* Mode B: flat grid plane */
} mesh3d_render_mode_t;

typedef enum {
    MESH3D_OVERLAY_NONE        = 0,
    MESH3D_OVERLAY_VIEWSHED    = 1, /* binary visible/not */
    MESH3D_OVERLAY_SIGNAL      = 2, /* signal strength heatmap */
    MESH3D_OVERLAY_LINK_MARGIN = 3  /* link margin (green/yellow/red) */
} mesh3d_overlay_mode_t;

#ifdef __cplusplus
}
#endif

#endif /* MESH3D_TYPES_H */
