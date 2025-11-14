#version 450
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D pathtracedImage;

in vec2 TexCoords;

void main() {
    fragColor = texture(pathtracedImage, TexCoords);
}