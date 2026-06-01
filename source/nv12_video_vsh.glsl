#version 460

layout (location = 0) out vec2 outTexCoord;
layout (location = 1) out vec2 outUvTexCoord;

layout (std140, binding = 0) uniform VideoParams
{
    vec4 texScale;
};

void main()
{
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 texcoords[3] = vec2[](
        vec2(0.0, 1.0),
        vec2(2.0, 1.0),
        vec2(0.0, -1.0)
    );

    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    outTexCoord = texcoords[gl_VertexID] * texScale.xy;
    outUvTexCoord = texcoords[gl_VertexID] * texScale.zw;
}
