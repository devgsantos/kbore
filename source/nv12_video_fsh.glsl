#version 460

layout (location = 0) in vec2 inTexCoord;
layout (location = 1) in vec2 inUvTexCoord;
layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D texY;
layout (binding = 1) uniform sampler2D texUV;

void main()
{
    float y = texture(texY, inTexCoord).r;
    vec2 uv = texture(texUV, inUvTexCoord).rg - vec2(0.5, 0.5);

    float yy = 1.16438356 * (y - 0.0625);
    float r = yy + 1.59602678 * uv.y;
    float g = yy - 0.39176229 * uv.x - 0.81296764 * uv.y;
    float b = yy + 2.01723214 * uv.x;

    outColor = vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
}
