#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aViewshed;
layout(location = 4) in float aSignalDbm;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out float vViewshed;
out float vSignalDbm;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUV = aUV;
    vViewshed = aViewshed;
    vSignalDbm = aSignalDbm;
    gl_Position = uProj * uView * world;
}
