#version 450
layout(location = 0) out vec4 fragColor;
layout(location = 0) in vec2 TexCoords;
layout(binding = 0) uniform sampler2D pathtracedImage;

vec3 ACESFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x*(a*x + b)) / (x*(c*x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(pathtracedImage, TexCoords).rgb;
    //hdr *= 0.5;
    //vec3 ldr = ACESFilm(hdr);
    fragColor = vec4(hdr, 1.0);
}