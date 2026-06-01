#version 460

layout (location = 0) out vec2 outTexCoord;

void main()
{
    vec2 positions[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0, -0.7111111),
        vec2(-1.0, -0.7111111),
        vec2( 1.0, -1.0),
        vec2( 1.0, -0.7111111)
    );
    vec2 texcoords[3] = vec2[](
        vec2(0.0, 1.0),
        vec2(1.0, 1.0),
        vec2(0.0, 0.0)
    );
    vec2 texcoords2[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 1.0),
        vec2(1.0, 0.0)
    );

    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    outTexCoord = gl_VertexID < 3 ? texcoords[gl_VertexID] : texcoords2[gl_VertexID - 3];
}
