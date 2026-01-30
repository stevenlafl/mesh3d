#version 330 core

in vec2 vUV;
in vec3 vWorldPos;

uniform int uOverlayMode;

out vec4 FragColor;

void main() {
    // Grid pattern
    vec2 grid = fract(vUV * 50.0);
    float line = step(0.02, grid.x) * step(0.02, grid.y);
    vec3 color = mix(vec3(0.4, 0.4, 0.45), vec3(0.25, 0.27, 0.3), line);

    FragColor = vec4(color, 1.0);
}
