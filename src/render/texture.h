#pragma once
#include <glad/glad.h>
#include <string>

namespace mesh3d {

class Texture {
public:
    Texture() = default;
    ~Texture();

    bool load(const std::string& path);
    bool load_rgba(const unsigned char* data, int w, int h);
    void bind(GLuint unit = 0) const;
    GLuint id() const { return m_tex; }
    bool valid() const { return m_tex != 0; }

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& o) noexcept;
    Texture& operator=(Texture&& o) noexcept;

private:
    GLuint m_tex = 0;
};

} // namespace mesh3d
