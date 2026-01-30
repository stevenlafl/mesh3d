#version 330 core

in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3 uColor;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(vec3(0.3, 1.0, 0.5));
    float diff = max(dot(N, L), 0.0);
    float ambient = 0.35;
    float lighting = ambient + diff * 0.65;

    vec3 color = uColor * lighting;
    FragColor = vec4(color, 1.0);
}
