#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in float vViewshed;
in float vSignalDbm;

uniform int uOverlayMode; // 0=none, 1=viewshed, 2=signal, 3=link_margin
uniform int uUseSatelliteTex;
uniform sampler2D uSatelliteTex;
uniform vec3 uLightDir;
uniform float uRxSensitivity; // dBm, for link margin overlay
uniform float uDisplayMinDbm; // bottom of signal color scale
uniform float uDisplayMaxDbm; // top of signal color scale

// GPU overlay textures (avoids mesh rebuild)
uniform int uUseOverlayTex;
uniform sampler2D uOverlayVisTex;  // R8 normalized: 0.0 or 1.0
uniform sampler2D uOverlaySigTex;  // R32F: dBm value

out vec4 FragColor;

vec3 signalColor(float dbm) {
    float t = clamp((dbm - uDisplayMinDbm) / (uDisplayMaxDbm - uDisplayMinDbm), 0.0, 1.0);
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

    // Resolve overlay values — GPU textures only; no vertex attribute fallback.
    // Tiles always use overlay textures; vertex attributes are unused in tile mode.
    float viewshed_val = 0.0;
    float signal_val = -999.0;
    if (uUseOverlayTex > 0) {
        viewshed_val = texture(uOverlayVisTex, vUV).r;
        signal_val = texture(uOverlaySigTex, vUV).r;
    }

    // Overlay — only draw where cell is visible and signal is within display range.
    // Display minimum: -130 dBm (bottom of signal color scale).
    // Areas below this threshold are left as clean map/terrain.
    float displayMin = uDisplayMinDbm;

    if (uOverlayMode == 1) {
        // Viewshed: tint covered areas green
        if (viewshed_val > 0.5 && signal_val >= displayMin) {
            color = mix(color, vec3(0.0, 1.0, 0.0), 0.35);
        }
    } else if (uOverlayMode == 2) {
        // Signal strength heatmap
        if (viewshed_val > 0.5 && signal_val >= displayMin) {
            vec3 sc = signalColor(signal_val);
            color = mix(color, sc, 0.5);
        }
    } else if (uOverlayMode == 3) {
        // Link margin overlay
        if (viewshed_val > 0.5 && signal_val >= uRxSensitivity) {
            float margin = signal_val - uRxSensitivity;
            vec3 mc;
            if (margin < 10.0) {
                mc = mix(vec3(1.0, 0.0, 0.0), vec3(1.0, 1.0, 0.0), margin / 10.0);
            } else if (margin < 20.0) {
                mc = mix(vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), (margin - 10.0) / 10.0);
            } else {
                mc = vec3(0.0, 1.0, 0.0);
            }
            color = mix(color, mc, 0.5);
        }
    }

    FragColor = vec4(color, 1.0);
}
