#include "render/texture.h"
#include "util/log.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace mesh3d {

Texture::~Texture() {
    if (m_tex) glDeleteTextures(1, &m_tex);
}

Texture::Texture(Texture&& o) noexcept : m_tex(o.m_tex) { o.m_tex = 0; }
Texture& Texture::operator=(Texture&& o) noexcept {
    if (this != &o) {
        if (m_tex) glDeleteTextures(1, &m_tex);
        m_tex = o.m_tex;
        o.m_tex = 0;
    }
    return *this;
}

bool Texture::load(const std::string& path) {
    stbi_set_flip_vertically_on_load(true);
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) {
        LOG_ERROR("Failed to load texture: %s", path.c_str());
        return false;
    }
    bool ok = load_rgba(data, w, h);
    stbi_image_free(data);
    return ok;
}

bool Texture::load_rgba(const unsigned char* data, int w, int h) {
    if (m_tex) glDeleteTextures(1, &m_tex);
    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Texture::bind(GLuint unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_tex);
}

} // namespace mesh3d
