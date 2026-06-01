#version 460

layout (location = 0) in vec2 inTexCoord;
layout (location = 1) in vec2 inUvTexCoord;
layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D texY;
layout (binding = 1) uniform sampler2D texUV;

layout (std140, binding = 0) uniform VideoParams
{
    vec4 texScale;
    vec4 coeffR;
    vec4 coeffG;
    vec4 coeffB;
};

void main()
{
    float y = texture(texY, inTexCoord).r;
    vec2 uv = texture(texUV, inUvTexCoord).rg;
    vec4 yuv = vec4(y, uv.x, uv.y, 1.0);

    outColor = vec4(
        clamp(vec3(dot(coeffR, yuv), dot(coeffG, yuv), dot(coeffB, yuv)), 0.0, 1.0),
        1.0
    );
}
