#version 330 core

in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3 uColor;
uniform vec3 uCameraPos;
uniform float uBaseAlpha;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);

    // Fresnel effect: more opaque at edges (grazing angles)
    float fresnel = 1.0 - abs(dot(N, V));
    fresnel = pow(fresnel, 2.0);

    float alpha = uBaseAlpha + fresnel * 0.4;
    alpha = clamp(alpha, 0.0, 0.7);

    // Soft lighting
    vec3 L = normalize(vec3(0.3, 1.0, 0.5));
    float diff = max(dot(N, L), 0.0);
    vec3 color = uColor * (0.5 + diff * 0.5);

    FragColor = vec4(color, alpha);
}
