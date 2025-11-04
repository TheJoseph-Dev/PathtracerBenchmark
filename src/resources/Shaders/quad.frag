#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UBO {
    vec4 resolution;  // vec4 because of padding
};

layout(set = 0, binding = 1) uniform sampler1D wSample;
layout(set = 0, binding = 2) uniform sampler1D wFreq;

float hash( float n )
{
    return fract(sin(n)*43745658.5453123);
}

float noise(vec2 pos)
{
	return fract( sin( dot(pos*0.001 ,vec2(24.12357, 36.789) ) ) * 12345.123);	
}

float noise(float r)
{
	return fract( sin( dot(vec2(r,-r)*0.001 ,vec2(24.12357, 36.789) ) ) * 12345.123);	
}

void main() {
    vec2 uv = fragUV;
    uv.y = 1.0-uv.y;
    uv = uv*2.0-1.0;
    uv.x *= resolution.x/resolution.y;
    float sValue = texture(wSample, fragUV.x).r;
    float fValue = texture(wFreq, fragUV.x).r;
    if(uv.y > 0) {
        float waveRemaped = (sValue + 1.2) * 0.4 * resolution.y;
        float thickness = 10.0;
        float dist = abs(uv.y*resolution.y - waveRemaped); //smoothstep(0.0, 1.0, );
        vec3 waveColor = vec3(0.65, 0.85, 1.0)/2.0; // Brighter color
        outColor = vec4(waveColor * pow((thickness/dist), 2.0), 1.0); // Stronger glow

        //if(uv.y < (sValue+1.0)*0.5) outColor = vec4(1.0);
        //else outColor = vec4(0.0);
    }
    else outColor = vec4(fValue);
}
