/*
  ==============================================================================

    The GLSL shaders SpriteRenderer draws with: the dots (each an instanced
    quad) and the backdrop behind them.

  ==============================================================================
*/

#pragma once

namespace threedq::shaders
{

inline constexpr const char* sprite = R"(#version 150
in vec2 aCorner;
in vec3 aCentreSize;
in vec3 aColour;
in vec2 aAlphaProfile;
uniform vec2 uViewport;
out vec2 vCoord;
out vec3 vColour;
out float vSize;
out float vAlpha;
out float vProfile;

void main()
{
    vec2 p = aCentreSize.xy + aCorner * aCentreSize.z * 0.5;
    gl_Position = vec4 (p.x / uViewport.x * 2.0 - 1.0, 1.0 - p.y / uViewport.y * 2.0, 0.0, 1.0);
    vCoord = aCorner;
    vColour = aColour;
    vSize = aCentreSize.z;
    vAlpha = aAlphaProfile.x;
    vProfile = aAlphaProfile.y;
}
)";

// Sprite profiles (t = 0 centre .. 1 edge), as premultiplied colour.
inline constexpr const char* spriteFragment = R"(#version 150
in vec2 vCoord;
in vec3 vColour;
in float vSize;
in float vAlpha;
in float vProfile;
out vec4 fragColour;

void main()
{
    float t = length (vCoord);
    if (t >= 1.0)
        discard;

    if (vProfile < 0.5)
    {
        // Particle: a small crisp core inside a faint wisp of gas. The core's
        // size is mostly in pixels, so big sprites stay sharp instead of
        // stretching into a blur. coord.y runs down the screen.
        float radiusPx = max (vSize * 0.5, 1.0);
        float coreR = min ((2.0 + 0.12 * radiusPx) / radiusPx, 1.0);
        float core = pow (max (1.0 - t * t / (coreR * coreR), 0.0), 2.0);
        // The wisp thins as the sprite grows, or big sprites overlap into a
        // pale fog over the colours.
        float wisp = 0.2 * min (3.0 / radiusPx, 1.0);
        float density = 0.82 * core + wisp * pow (1.0 - t * t, 3.0);
        float shade = 0.72 + 0.35 * density + 0.3 * dot (vCoord, vec2 (-0.56, -0.83));

        // Covers most of what's behind it, so overlapping puffs stay their colour.
        fragColour = vec4 (vColour * shade * density * vAlpha, 0.8 * density * vAlpha);
        return;
    }

    float glow = t < 0.4 ? mix (1.0, 0.35, t / 0.4) : mix (0.35, 0.0, (t - 0.4) / 0.6);
    fragColour = vec4 (vColour * glow * vAlpha, 0.0); // alpha 0: purely additive
}
)";

inline constexpr const char* backdrop = R"(#version 150
void main()
{
    gl_Position = vec4 (float (gl_VertexID & 1) * 2.0 - 1.0, float ((gl_VertexID >> 1) & 1) * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Graphite, lit softly behind the shape, darker toward the corners, with a soft
// shadow under it. The haze puts lows, mids and highs at the heights they have
// on the sphere.
inline constexpr const char* backdropFragment = R"(#version 150
uniform vec2 uViewport;
uniform vec3 uLight;  // x, y of the shape's middle, sphere radius
uniform float uSphereY;
uniform vec2 uShadow; // y, half width
uniform vec3 uHaze;
out vec4 fragColour;

float gaussian (vec2 d, vec2 sigma)
{
    d /= sigma;
    return exp (-dot (d, d));
}

void main()
{
    vec2 p = vec2 (gl_FragCoord.x, uViewport.y - gl_FragCoord.y);
    float radius = uLight.z;
    vec3 c = vec3 (17.0, 17.0, 16.0) / 255.0;

    // A big sphere shows more of the backdrop between its dots: less glow
    // there keeps the colours from washing out.
    float glow = clamp (220.0 / radius, 0.35, 1.0);

    c += vec3 (1.0, 0.98, 0.92) * 0.042 * glow * gaussian (p - uLight.xy, vec2 (1.7 * radius));

    vec2 sigma = vec2 (1.5, 0.85) * radius;
    float x = p.x - uLight.x;
    c += vec3 (0.35, 0.59, 0.82) * 0.075 * glow * uHaze.x * gaussian (vec2 (x, p.y - uSphereY - 0.7 * radius), sigma);
    c += vec3 (0.47, 0.76, 0.55) * 0.075 * glow * uHaze.y * gaussian (vec2 (x, p.y - uSphereY), sigma);
    c += vec3 (0.92, 0.87, 0.67) * 0.075 * glow * uHaze.z * gaussian (vec2 (x, p.y - uSphereY + 0.7 * radius), sigma);

    c *= 1.0 - 0.3 * smoothstep (0.75, 1.45, length ((p - 0.5 * uViewport) / (0.5 * uViewport)));
    c *= 1.0 - 0.65 * gaussian (p - vec2 (uLight.x, uShadow.x), vec2 (max (uShadow.y, 1.0), 0.13 * radius));

    // A step of 1/255 is visible on a dark gradient; noise hides it.
    float noise = fract (52.9829189 * fract (dot (gl_FragCoord.xy, vec2 (0.06711056, 0.00583715))));
    fragColour = vec4 (c + (noise - 0.5) / 255.0, 1.0);
}
)";

} // namespace threedq::shaders
