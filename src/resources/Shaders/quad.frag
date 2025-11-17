#version 450
layout(location = 0) out vec4 fragColor;
layout(location = 0) in vec2 TexCoords;
layout(binding = 0) uniform sampler2D pathtracedImage;

void main() {
    fragColor = texture(pathtracedImage, TexCoords);
    //fragColor = vec4(0.3, 0.5, 0.8, 1.0);
}