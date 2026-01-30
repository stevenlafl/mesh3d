#include "analysis/itm.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace mesh3d {

/* ────────────────────────────────────────────────────────────────────
   Simplified Longley-Rice (ITM) point-to-point propagation model.
   Based on NTIA reference implementation (public domain).

   Implements:
     - Two-ray ground reflection model for short paths
     - Smooth-earth diffraction for medium paths
     - Tropospheric scatter for long paths
     - Terrain roughness (delta_h) adjustment
     - Climate and ground parameter effects
   ──────────────────────────────────────────────────────────────────── */

static constexpr double PI     = 3.141592653589793;
static constexpr double RE_M   = 6371000.0;  // earth radius
static constexpr double K_EFF  = 4.0 / 3.0;  // effective earth radius factor

/* Compute terrain roughness parameter (delta_h):
   Interdecile range of terrain heights in the profile (10th to 90th percentile range). */
static float compute_delta_h(const float* profile, int n) {
    if (n < 3) return 0.0f;

    std::vector<float> sorted(profile + 1, profile + n - 1); // exclude endpoints
    if (sorted.empty()) return 0.0f;
    std::sort(sorted.begin(), sorted.end());

    int i10 = static_cast<int>(sorted.size() * 0.1);
    int i90 = static_cast<int>(sorted.size() * 0.9);
    i90 = std::min(i90, static_cast<int>(sorted.size()) - 1);
    return sorted[i90] - sorted[i10];
}

/* Free-space loss in dB */
static double free_space_loss(double dist_m, double freq_mhz) {
    double dist_km = dist_m / 1000.0;
    if (dist_km < 0.01) dist_km = 0.01;
    return 20.0 * std::log10(dist_km) + 20.0 * std::log10(freq_mhz) + 32.44;
}

/* Smooth-earth horizon distance for an antenna at height h_m */
static double horizon_distance(double h_m) {
    return std::sqrt(2.0 * K_EFF * RE_M * h_m);
}

/* Two-ray (plane earth) loss in dB — used for very short paths */
static double two_ray_loss(double dist_m, double h1, double h2) {
    /* L = 120 - 20*log10(h1*h2) + 40*log10(d) */
    if (h1 < 1.0) h1 = 1.0;
    if (h2 < 1.0) h2 = 1.0;
    if (dist_m < 1.0) dist_m = 1.0;
    return 120.0 - 20.0 * std::log10(h1 * h2) + 40.0 * std::log10(dist_m);
}

/* Smooth-earth diffraction loss (Bullington method with Norton approximation) */
static double diffraction_loss(double dist_m, double freq_mhz,
                                 double h1, double h2, double delta_h) {
    double lambda = 299.792458 / freq_mhz; // meters
    double ae = K_EFF * RE_M;

    /* Effective heights adjusted for terrain roughness */
    double he1 = h1;
    double he2 = h2;
    if (delta_h > 0) {
        /* Terrain clutter reduces effective height */
        he1 = std::max(h1 - 0.1 * delta_h, 1.0);
        he2 = std::max(h2 - 0.1 * delta_h, 1.0);
    }

    /* Horizon distances */
    double dl1 = horizon_distance(he1);
    double dl2 = horizon_distance(he2);
    double dls = dl1 + dl2; // smooth-earth line-of-sight distance

    if (dist_m <= dls) {
        /* Within line-of-sight: minor diffraction loss */
        double ratio = dist_m / dls;
        return 6.0 * ratio * ratio; // gentle rolloff
    }

    /* Beyond line-of-sight: knife-edge diffraction loss */
    double d_excess = dist_m - dls;
    /* Equivalent knife-edge clearance parameter */
    double v = 2.0 * d_excess / std::sqrt(lambda * dist_m);
    if (v < -0.78)
        return 0.0;
    return 6.9 + 20.0 * std::log10(std::sqrt((v - 0.1) * (v - 0.1) + 1.0) + v - 0.1);
}

/* Tropospheric scatter loss — for very long paths (>100km typically) */
static double scatter_loss(double dist_m, double freq_mhz,
                            double h1, double h2, int climate) {
    /* Simplified Yeh scatter model */
    double dist_km = dist_m / 1000.0;
    if (dist_km < 10.0) return 0.0; // not applicable for short paths

    /* Climate-dependent scatter coefficient */
    static const double N_s[] = {
        0.0, 360.0, 320.0, 370.0, 325.0, 310.0, 350.0, 295.0
    };
    double ns = (climate >= 1 && climate <= 7) ? N_s[climate] : 310.0;

    /* Angular distance (radians) */
    double theta = dist_m / (K_EFF * RE_M);

    /* Scatter loss (simplified ITM formula) */
    double loss = 190.0 - 10.0 * std::log10(ns)
                + 20.0 * std::log10(freq_mhz)
                + 30.0 * std::log10(theta)
                - 0.27 * ns;

    return std::max(loss, 0.0);
}

/* Ground impedance effects */
static double ground_loss(double freq_mhz, double dielectric,
                            double conductivity, int polarization) {
    /* Simplified ground reflection coefficient effect */
    double omega = 2.0 * PI * freq_mhz * 1e6;
    double epsilon0 = 8.854e-12;
    double ratio = conductivity / (omega * epsilon0 * dielectric);

    /* Additional loss from ground proximity */
    double loss = 0.0;
    if (polarization == 0) { // horizontal
        loss = 2.0 + 3.0 * std::log10(1.0 + ratio);
    } else { // vertical
        loss = 1.0 + 2.0 * std::log10(1.0 + ratio);
    }
    return std::max(loss, 0.0);
}

float itm_point_to_point(const float* profile, int n_profile, float step_m,
                          float tx_height, float rx_height,
                          float freq_mhz, const mesh3d_itm_params_t& params) {
    if (n_profile < 2 || step_m <= 0 || freq_mhz <= 0)
        return 999.0f;

    double dist_m = static_cast<double>(n_profile - 1) * step_m;
    if (dist_m < 1.0) return 0.0f;

    /* Compute terrain roughness */
    float delta_h = compute_delta_h(profile, n_profile);

    /* Effective antenna heights above local terrain */
    double h1 = static_cast<double>(tx_height);
    double h2 = static_cast<double>(rx_height);

    /* Free-space loss (baseline) */
    double fsl = free_space_loss(dist_m, freq_mhz);

    /* Diffraction loss from terrain */
    double dfl = diffraction_loss(dist_m, freq_mhz, h1, h2, delta_h);

    /* Ground effects */
    double gnd = ground_loss(freq_mhz, params.ground_dielectric,
                              params.ground_conductivity, params.polarization);

    /* Scatter loss (long paths) */
    double scl = scatter_loss(dist_m, freq_mhz, h1, h2, params.climate);

    /* ITM combines these via smooth transitions.
       For paths within LOS: primarily free-space + ground effects
       For diffraction paths: free-space + diffraction + ground
       For scatter paths: use scatter loss (which includes free-space component) */

    double dls = horizon_distance(h1) + horizon_distance(h2);

    double total_loss;
    if (dist_m < dls * 0.5) {
        /* Well within LOS: free-space + minor terrain effects */
        total_loss = fsl + gnd + 0.1 * delta_h / std::max(h1, 1.0);
    } else if (dist_m < dls * 2.0) {
        /* Transition zone: blend free-space+diffraction */
        double t = (dist_m - dls * 0.5) / (dls * 1.5);
        t = std::clamp(t, 0.0, 1.0);
        double los_loss = fsl + gnd;
        double diff_total = fsl + dfl + gnd;
        total_loss = los_loss * (1.0 - t) + diff_total * t;
    } else if (scl > fsl + dfl + gnd) {
        /* Very long path: tropospheric scatter dominates */
        double t = std::clamp((dist_m / dls - 2.0) / 3.0, 0.0, 1.0);
        double diff_total = fsl + dfl + gnd;
        total_loss = diff_total * (1.0 - t) + scl * t;
    } else {
        /* Diffraction zone */
        total_loss = fsl + dfl + gnd;
    }

    /* Terrain roughness adjustment */
    if (delta_h > 10.0f) {
        /* Higher terrain roughness adds loss due to multipath and shadowing */
        total_loss += 0.5 * std::log10(delta_h / 10.0) * 10.0;
    }

    return static_cast<float>(total_loss);
}

std::vector<float> extract_profile(const float* elevation, int rows, int cols,
                                    int r0, int c0, int r1, int c1,
                                    float cell_meters, int max_samples,
                                    float& out_step_m) {
    int dr = r1 - r0;
    int dc = c1 - c0;
    float dist_cells = std::sqrt(static_cast<float>(dr * dr + dc * dc));
    int n_samples = static_cast<int>(dist_cells) + 1;

    if (n_samples < 2) {
        out_step_m = cell_meters;
        return {elevation[r0 * cols + c0], elevation[r1 * cols + c1]};
    }

    /* Subsample if path is longer than max_samples */
    int step = 1;
    if (n_samples > max_samples) {
        step = (n_samples + max_samples - 1) / max_samples;
        n_samples = (n_samples + step - 1) / step;
    }

    std::vector<float> profile;
    profile.reserve(n_samples);

    for (int i = 0; i < n_samples; ++i) {
        float t = static_cast<float>(i * step) / (dist_cells);
        t = std::min(t, 1.0f);
        float fr = r0 + dr * t;
        float fc = c0 + dc * t;
        int ir = std::clamp(static_cast<int>(fr), 0, rows - 1);
        int ic = std::clamp(static_cast<int>(fc), 0, cols - 1);
        profile.push_back(elevation[ir * cols + ic]);
    }

    /* Ensure last sample is the target cell */
    if (!profile.empty()) {
        profile.back() = elevation[std::clamp(r1, 0, rows - 1) * cols +
                                    std::clamp(c1, 0, cols - 1)];
    }

    out_step_m = cell_meters * step;
    return profile;
}

} // namespace mesh3d
