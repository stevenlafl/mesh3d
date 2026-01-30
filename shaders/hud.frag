#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform int uUseTexture; // 0=solid, 1=font atlas
out vec4 fragColor;
void main() {
    if (uUseTexture == 1) {
        float a = texture(uTex, vUV).r;
        fragColor = vec4(uColor.rgb, uColor.a * a);
    } else {
        fragColor = uColor;
    }
}
