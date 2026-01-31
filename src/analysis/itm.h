#pragma once
#include <mesh3d/types.h>
#include <vector>

namespace mesh3d {

/* Default ITM parameters for Meshtastic (continental temperate, vertical polarization) */
inline mesh3d_itm_params_t itm_defaults() {
    return {
        5,       // climate: continental temperate
        15.0f,   // ground_dielectric
        0.005f,  // ground_conductivity S/m
        1,       // polarization: vertical
        50.0f,   // situation_pct
        50.0f,   // time_pct
        301.0f,  // refractivity (N_0)
        50.0f,   // location_pct
        12       // mdvar (broadcast + eliminate direct situation variability)
    };
}

/* Longley-Rice ITM point-to-point path loss calculation (CPU reference).
   profile:    terrain elevation samples along the path (meters), evenly spaced
   n_profile:  number of profile samples
   step_m:     spacing between profile samples (meters)
   tx_height:  transmitter height above ground (meters)
   rx_height:  receiver height above ground (meters)
   freq_mhz:   frequency in MHz
   params:     ITM climate/ground parameters
   Returns:    median path loss in dB */
float itm_point_to_point(const float* profile, int n_profile, float step_m,
                          float tx_height, float rx_height,
                          float freq_mhz, const mesh3d_itm_params_t& params);

/* Extract terrain profile between two grid cells.
   Returns evenly-spaced elevation samples along the path.
   elevation:  row-major elevation grid
   rows, cols: grid dimensions
   r0, c0:     source cell (row, col)
   r1, c1:     target cell (row, col)
   max_samples: maximum profile length (subsamples if path is longer)
   out_step_m:  output: spacing between samples in meters */
std::vector<float> extract_profile(const float* elevation, int rows, int cols,
                                    int r0, int c0, int r1, int c1,
                                    float cell_meters, int max_samples,
                                    float& out_step_m);

} // namespace mesh3d
