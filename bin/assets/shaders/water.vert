#version 300 es
precision highp float;

// MeshBuffer layout: loc0=position, loc1=normal, loc2=tangent(vec4), loc3=uv
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 3) in vec2 aTexCoord;

out vec4 ClipSpace;
out vec2 TexCoord;
out vec3 ToCameraVector;
out vec3 WorldPos;
out vec2 v_bumpMapTexCoord;
out vec3 v_normal;

uniform mat4 u_viewProj;
uniform vec4 u_cameraPos;

uniform mat4  u_model;
uniform float u_time;
uniform float u_waveLength;
uniform float u_windForce;
uniform vec2  u_windDirection;
uniform float u_waveHeight;

uniform vec4 u_wave1;
uniform vec4 u_wave2;
uniform vec4 u_wave3;
uniform vec4 u_wave4;

const float PI = 3.14159265359;

vec3 GerstnerWave(vec2 position, vec2 direction, float steepness, float wavelength, float time)
{
    float k = 2.0 * PI / wavelength;
    float c = sqrt(9.8 / k);
    vec2 d = normalize(direction);
    float f = k * (dot(d, position) - c * time);
    float a = steepness / k;
    return vec3(
        d.x * a * cos(f),
        a * sin(f),
        d.y * a * cos(f)
    );
}

vec3 GerstnerWaveNormal(vec2 position, vec2 direction, float steepness, float wavelength, float time)
{
    float k = 2.0 * PI / wavelength;
    float c = sqrt(9.8 / k);
    vec2 d = normalize(direction);
    float f = k * (dot(d, position) - c * time);
    float dx = -d.x * d.x * steepness * sin(f);
    float dz = -d.x * d.y * steepness * sin(f);
    return vec3(-dx, 1.0, -dz);
}

void main()
{
    vec3 pos = aPos;
    vec3 normal = aNormal;

    vec3 waveOffset = vec3(0.0);
    vec3 waveNormal = vec3(0.0, 1.0, 0.0);

    waveOffset += GerstnerWave      (pos.xz, u_wave1.xy, u_wave1.z, u_wave1.w, u_time);
    waveNormal += GerstnerWaveNormal(pos.xz, u_wave1.xy, u_wave1.z, u_wave1.w, u_time);

    waveOffset += GerstnerWave      (pos.xz, u_wave2.xy, u_wave2.z, u_wave2.w, u_time);
    waveNormal += GerstnerWaveNormal(pos.xz, u_wave2.xy, u_wave2.z, u_wave2.w, u_time);

    waveOffset += GerstnerWave      (pos.xz, u_wave3.xy, u_wave3.z, u_wave3.w, u_time);
    waveNormal += GerstnerWaveNormal(pos.xz, u_wave3.xy, u_wave3.z, u_wave3.w, u_time);

    waveOffset += GerstnerWave      (pos.xz, u_wave4.xy, u_wave4.z, u_wave4.w, u_time);
    waveNormal += GerstnerWaveNormal(pos.xz, u_wave4.xy, u_wave4.z, u_wave4.w, u_time);

    pos   += waveOffset * u_waveHeight;
    normal = normalize(waveNormal);

    vec4 worldPos = u_model * vec4(pos, 1.0);
    WorldPos      = worldPos.xyz;
    ClipSpace     = u_viewProj * worldPos;
    gl_Position   = ClipSpace;
    
    TexCoord      = aTexCoord;
    ToCameraVector = u_cameraPos.xyz - worldPos.xyz;
    v_normal      = normal;

    v_bumpMapTexCoord = aTexCoord / u_waveLength + u_time * u_windForce * u_windDirection;
}
