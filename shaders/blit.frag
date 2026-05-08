#version 450

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

// The Skia-rendered page texture (bound as combined image sampler).
layout(set = 0, binding = 0) uniform sampler2D pageTexture;

void main()
{
    outColor = texture(pageTexture, fragUV);
}
