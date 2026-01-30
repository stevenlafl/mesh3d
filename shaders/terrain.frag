#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in float vViewshed;
in float vSignalDbm;

uniform int uOverlayMode; // 0=none, 1=viewshed, 2=signal
uniform int uUseSatelliteTex;
uniform sampler2D uSatelliteTex;
uniform vec3 uLightDir;

out vec4 FragColor;

vec3 signalColor(float dbm) {
    float t = clamp((dbm - (-130.0)) / ((-80.0) - (-130.0)), 0.0, 1.0);
    // red(weak) -> yellow(mid) -> green(strong)
    vec3 c;
    if (t < 0.5) {
        c = mix(vec3(1.0, 0.0, 0.0), vec3(1.0, 1.0, 0.0), t * 2.0);
    } else {
        c = mix(vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), (t - 0.5) * 2.0);
    }
    return c;
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    float diff = max(dot(N, L), 0.0);
    float ambient = 0.3;
    float lighting = ambient + diff * 0.7;

    // Base color: height-based terrain coloring
    float h = vWorldPos.y;
    vec3 baseColor;
    if (h < 150.0) {
        baseColor = mix(vec3(0.2, 0.5, 0.2), vec3(0.4, 0.6, 0.3), h / 150.0);
    } else if (h < 300.0) {
        baseColor = mix(vec3(0.4, 0.6, 0.3), vec3(0.6, 0.5, 0.3), (h - 150.0) / 150.0);
    } else {
        baseColor = mix(vec3(0.6, 0.5, 0.3), vec3(0.9, 0.9, 0.9), clamp((h - 300.0) / 200.0, 0.0, 1.0));
    }

    // Satellite texture override
    if (uUseSatelliteTex > 0) {
        baseColor = texture(uSatelliteTex, vUV).rgb;
    }

    vec3 color = baseColor * lighting;

    // Overlay
    if (uOverlayMode == 1) {
        // Viewshed: tint visible areas green
        if (vViewshed > 0.5) {
            color = mix(color, vec3(0.0, 1.0, 0.0), 0.35);
        } else {
            color *= 0.5; // darken non-visible
        }
    } else if (uOverlayMode == 2) {
        // Signal strength heatmap
        if (vSignalDbm > -900.0) {
            vec3 sc = signalColor(vSignalDbm);
            color = mix(color, sc, 0.6);
        }
    }

    FragColor = vec4(color, 1.0);
}
