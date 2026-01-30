#include "analysis/gpu_viewshed.h"
#include "util/log.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace mesh3d {

GpuViewshed::~GpuViewshed() {
    shutdown();
}

bool GpuViewshed::is_available() {
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    return (major > 4) || (major == 4 && minor >= 3);
}

bool GpuViewshed::init(const std::string& shader_dir) {
    if (!is_available()) {
        LOG_WARN("GPU viewshed: compute shaders not available (need GL 4.3+)");
        return false;
    }

    if (!m_viewshed_shader.load(shader_dir + "/viewshed.comp")) {
        LOG_ERROR("GPU viewshed: failed to load viewshed.comp");
        return false;
    }

    if (!m_merge_shader.load(shader_dir + "/viewshed_merge.comp")) {
        LOG_ERROR("GPU viewshed: failed to load viewshed_merge.comp");
        return false;
    }

    /* ITM and Fresnel shaders are optional */
    m_has_itm = m_itm_shader.load(shader_dir + "/itm.comp");
    if (!m_has_itm) {
        LOG_WARN("GPU viewshed: itm.comp not found, ITM model unavailable");
    }

    m_has_fresnel = m_fresnel_shader.load(shader_dir + "/fresnel.comp");
    if (!m_has_fresnel) {
        LOG_WARN("GPU viewshed: fresnel.comp not found, Fresnel model unavailable");
    }

    m_initialized = true;
    LOG_INFO("GPU viewshed compute shaders initialized (ITM=%s, Fresnel=%s)",
             m_has_itm ? "yes" : "no", m_has_fresnel ? "yes" : "no");
    return true;
}

void GpuViewshed::shutdown() {
    destroy_textures();
    m_initialized = false;
}

void GpuViewshed::set_propagation_model(mesh3d_prop_model_t model) {
    if (model == MESH3D_PROP_ITM && !m_has_itm) {
        LOG_WARN("ITM propagation model not available, keeping current model");
        return;
    }
    if (model == MESH3D_PROP_FRESNEL && !m_has_fresnel) {
        LOG_WARN("Fresnel propagation model not available, keeping current model");
        return;
    }
    m_prop_model = model;
    const char* names[] = {"FSPL", "ITM", "Fresnel"};
    LOG_INFO("Propagation model: %s", names[static_cast<int>(model)]);
}

void GpuViewshed::set_itm_params(const mesh3d_itm_params_t& params) {
    m_itm_params = params;
}

void GpuViewshed::create_textures(int rows, int cols) {
    if (m_rows == rows && m_cols == cols && m_elevation_tex != 0)
        return; // already allocated at correct size

    destroy_textures();
    m_rows = rows;
    m_cols = cols;

    auto make_r32f = [&]() -> GLuint {
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, cols, rows);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return tex;
    };

    auto make_r8ui = [&]() -> GLuint {
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8UI, cols, rows);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return tex;
    };

    m_elevation_tex  = make_r32f();
    m_node_vis_tex   = make_r8ui();
    m_node_sig_tex   = make_r32f();
    m_merged_vis_tex = make_r8ui();
    m_merged_sig_tex = make_r32f();
    m_overlap_tex    = make_r8ui();

    glBindTexture(GL_TEXTURE_2D, 0);
}

void GpuViewshed::destroy_textures() {
    GLuint textures[] = {
        m_elevation_tex, m_node_vis_tex, m_node_sig_tex,
        m_merged_vis_tex, m_merged_sig_tex, m_overlap_tex
    };
    for (auto& t : textures) {
        if (t) { glDeleteTextures(1, &t); t = 0; }
    }
    m_rows = 0;
    m_cols = 0;
}

void GpuViewshed::clear_merge_textures() {
    /* Set pixel unpack alignment to 1 for R8UI textures (cols may not be multiple of 4) */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    int total = m_rows * m_cols;
    {
        std::vector<uint8_t> zero_u8(total, 0);
        glBindTexture(GL_TEXTURE_2D, m_merged_vis_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_cols, m_rows,
                        GL_RED_INTEGER, GL_UNSIGNED_BYTE, zero_u8.data());
        glBindTexture(GL_TEXTURE_2D, m_overlap_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_cols, m_rows,
                        GL_RED_INTEGER, GL_UNSIGNED_BYTE, zero_u8.data());
    }
    {
        std::vector<float> neg999(total, -999.0f);
        glBindTexture(GL_TEXTURE_2D, m_merged_sig_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_cols, m_rows,
                        GL_RED, GL_FLOAT, neg999.data());
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // restore default
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GpuViewshed::upload_elevation(const float* data, int rows, int cols) {
    create_textures(rows, cols);

    glBindTexture(GL_TEXTURE_2D, m_elevation_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cols, rows,
                    GL_RED, GL_FLOAT, data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GpuViewshed::set_grid_params(const mesh3d_bounds_t& bounds, int rows, int cols) {
    m_bounds = bounds;

    double lat_res = (bounds.max_lat - bounds.min_lat) / (rows - 1);
    double lon_res = (bounds.max_lon - bounds.min_lon) / (cols - 1);
    double center_lat = (bounds.min_lat + bounds.max_lat) * 0.5;
    double m_per_deg_lat = 111320.0;
    double m_per_deg_lon = 111320.0 * std::cos(center_lat * M_PI / 180.0);
    float cell_m_lat = static_cast<float>(lat_res * m_per_deg_lat);
    float cell_m_lon = static_cast<float>(lon_res * m_per_deg_lon);
    m_cell_meters = (cell_m_lat + cell_m_lon) * 0.5f;
}

void GpuViewshed::compute_all(const std::vector<NodeData>& nodes) {
    if (!m_initialized || m_rows == 0 || m_cols == 0) return;

    clear_merge_textures();

    /* Compute workgroup counts (ceil division) */
    GLuint groups_x = (m_cols + 15) / 16;
    GLuint groups_y = (m_rows + 15) / 16;

    /* Precompute grid coordinate mapping */
    double lat_res = (m_bounds.max_lat - m_bounds.min_lat) / (m_rows - 1);
    double lon_res = (m_bounds.max_lon - m_bounds.min_lon) / (m_cols - 1);

    /* Select compute shader based on propagation model */
    ComputeShader* active_shader = &m_viewshed_shader;
    if (m_prop_model == MESH3D_PROP_ITM && m_has_itm)
        active_shader = &m_itm_shader;
    else if (m_prop_model == MESH3D_PROP_FRESNEL && m_has_fresnel)
        active_shader = &m_fresnel_shader;

    for (auto& nd : nodes) {
        /* Map node to grid cell */
        int nr = static_cast<int>((m_bounds.max_lat - nd.info.lat) / lat_res);
        int nc = static_cast<int>((nd.info.lon - m_bounds.min_lon) / lon_res);
        nr = std::clamp(nr, 0, m_rows - 1);
        nc = std::clamp(nc, 0, m_cols - 1);

        /* Read node elevation at grid cell from GPU texture */
        float node_elev = 0.0f;
        {
            GLuint fbo;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, m_elevation_tex, 0);
            glReadPixels(nc, nr, 1, 1, GL_RED, GL_FLOAT, &node_elev);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
        }

        float antenna_h = nd.info.antenna_height_m;
        if (antenna_h < 1.0f) antenna_h = 2.0f;
        float obs_h = node_elev + antenna_h;

        float max_range_km = nd.info.max_range_km;
        if (max_range_km <= 0) max_range_km = 5.0f;
        int max_range_cells = static_cast<int>(max_range_km * 1000.0f / m_cell_meters);
        if (max_range_cells < 1) max_range_cells = 1;

        float tx_power_dbm = nd.info.tx_power_dbm;
        if (tx_power_dbm <= 0) tx_power_dbm = 22.0f;
        float antenna_gain = nd.info.antenna_gain_dbi;
        float freq_mhz = nd.info.frequency_mhz;
        if (freq_mhz <= 0) freq_mhz = 906.875f;
        float cable_loss = nd.info.cable_loss_db;
        float rx_sens = nd.info.rx_sensitivity_dbm;
        if (rx_sens >= 0) rx_sens = -132.0f;

        /* Earth curvature factor: 1 / (2 * k * Re) where k=4/3, Re=6371000m */
        float earth_curve_factor = 1.0f / (2.0f * (4.0f / 3.0f) * 6371000.0f);

        /* --- Viewshed pass --- */
        active_shader->use();
        active_shader->set_ivec2("uGridSize", m_cols, m_rows);
        active_shader->set_ivec2("uNodeCell", nc, nr);
        active_shader->set_float("uObserverHeight", obs_h);
        active_shader->set_int("uMaxRangeCells", max_range_cells);
        active_shader->set_float("uTxPowerDbm", tx_power_dbm);
        active_shader->set_float("uAntennaGainDbi", antenna_gain);
        active_shader->set_float("uFreqMhz", freq_mhz);
        active_shader->set_float("uCellMeters", m_cell_meters);
        active_shader->set_float("uCableLossDb", cable_loss);
        active_shader->set_float("uRxSensitivityDbm", rx_sens);
        active_shader->set_float("uEarthCurveFactor", earth_curve_factor);

        /* ITM-specific uniforms */
        if (m_prop_model == MESH3D_PROP_ITM && m_has_itm) {
            active_shader->set_int("uClimate", m_itm_params.climate);
            active_shader->set_float("uGroundDielectric", m_itm_params.ground_dielectric);
            active_shader->set_float("uGroundConductivity", m_itm_params.ground_conductivity);
            active_shader->set_int("uPolarization", m_itm_params.polarization);
            active_shader->set_float("uTargetHeight", 2.0f);
        }

        /* Fresnel-specific uniforms */
        if (m_prop_model == MESH3D_PROP_FRESNEL && m_has_fresnel) {
            active_shader->set_float("uTargetHeight", 2.0f);
        }

        glBindImageTexture(0, m_elevation_tex, 0, GL_FALSE, 0, GL_READ_ONLY,  GL_R32F);
        glBindImageTexture(1, m_node_vis_tex,  0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8UI);
        glBindImageTexture(2, m_node_sig_tex,  0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

        active_shader->dispatch(groups_x, groups_y, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        /* --- Merge pass --- */
        m_merge_shader.use();
        m_merge_shader.set_ivec2("uGridSize", m_cols, m_rows);

        glBindImageTexture(0, m_node_vis_tex,   0, GL_FALSE, 0, GL_READ_ONLY,  GL_R8UI);
        glBindImageTexture(1, m_node_sig_tex,   0, GL_FALSE, 0, GL_READ_ONLY,  GL_R32F);
        glBindImageTexture(2, m_merged_vis_tex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R8UI);
        glBindImageTexture(3, m_merged_sig_tex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);
        glBindImageTexture(4, m_overlap_tex,    0, GL_FALSE, 0, GL_READ_WRITE, GL_R8UI);

        m_merge_shader.dispatch(groups_x, groups_y, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }
}

void GpuViewshed::read_back(std::vector<uint8_t>& vis,
                              std::vector<float>& signal,
                              std::vector<uint8_t>& overlap) {
    if (m_rows == 0 || m_cols == 0) return;

    int total = m_rows * m_cols;
    vis.resize(total);
    signal.resize(total);
    overlap.resize(total);

    /* Set pack alignment to 1 for R8UI textures (cols may not be multiple of 4) */
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    /* Read merged visibility */
    glBindTexture(GL_TEXTURE_2D, m_merged_vis_tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, vis.data());

    /* Read merged signal */
    glBindTexture(GL_TEXTURE_2D, m_merged_sig_tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, signal.data());

    /* Read overlap count */
    glBindTexture(GL_TEXTURE_2D, m_overlap_tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, overlap.data());

    glPixelStorei(GL_PACK_ALIGNMENT, 4); // restore default
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace mesh3d
