#version 450

// Full-screen triangle trick: no vertex buffer needed.
// gl_VertexIndex 0,1,2 → covers the entire NDC viewport.
layout(location = 0) out vec2 fragUV;

void main()
{
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    fragUV  = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
