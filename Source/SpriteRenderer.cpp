/*
  ==============================================================================

    SpriteRenderer: see SpriteRenderer.h.

    Each sprite is an instanced quad rather than a GL point: points have a
    driver-specific maximum size and vanish when their centre leaves the
    viewport, which would show up as popping at high zoom.

  ==============================================================================
*/

#include "SpriteRenderer.h"

using namespace juce::gl;

namespace sphere
{

namespace
{
const char* const vertexShader = R"(#version 150
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
const char* const fragmentShader = R"(#version 150
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
        float density = 0.82 * core + 0.2 * pow (1.0 - t * t, 3.0);
        float shade = 0.72 + 0.35 * density + 0.3 * dot (vCoord, vec2 (-0.56, -0.83));

        // Covers most of what's behind it, so overlapping puffs stay their colour.
        fragColour = vec4 (vColour * shade * density * vAlpha, 0.8 * density * vAlpha);
        return;
    }

    float glow = t < 0.4 ? mix (1.0, 0.35, t / 0.4) : mix (0.35, 0.0, (t - 0.4) / 0.6);
    fragColour = vec4 (vColour * glow * vAlpha, 0.0); // alpha 0: purely additive
}
)";

const char* const backdropVertexShader = R"(#version 150
void main()
{
    gl_Position = vec4 (float (gl_VertexID & 1) * 2.0 - 1.0, float ((gl_VertexID >> 1) & 1) * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Graphite, lit softly behind the shape, darker toward the corners, with a soft
// shadow under it. The haze puts lows, mids and highs at the heights they have
// on the sphere. Matches ui/src/backdrop.ts.
const char* const backdropFragmentShader = R"(#version 150
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

    c += vec3 (1.0, 0.98, 0.92) * 0.042 * gaussian (p - uLight.xy, vec2 (1.7 * radius));

    vec2 sigma = vec2 (1.5, 0.85) * radius;
    float x = p.x - uLight.x;
    c += vec3 (0.35, 0.59, 0.82) * 0.075 * uHaze.x * gaussian (vec2 (x, p.y - uSphereY - 0.7 * radius), sigma);
    c += vec3 (0.47, 0.76, 0.55) * 0.075 * uHaze.y * gaussian (vec2 (x, p.y - uSphereY), sigma);
    c += vec3 (0.92, 0.87, 0.67) * 0.075 * uHaze.z * gaussian (vec2 (x, p.y - uSphereY + 0.7 * radius), sigma);

    c *= 1.0 - 0.3 * smoothstep (0.75, 1.45, length ((p - 0.5 * uViewport) / (0.5 * uViewport)));
    c *= 1.0 - 0.65 * gaussian (p - vec2 (uLight.x, uShadow.x), vec2 (max (uShadow.y, 1.0), 0.13 * radius));

    // A step of 1/255 is visible on a dark gradient; noise hides it.
    float noise = fract (52.9829189 * fract (dot (gl_FragCoord.xy, vec2 (0.06711056, 0.00583715))));
    fragColour = vec4 (c + (noise - 0.5) / 255.0, 1.0);
}
)";

void* offsetOf (size_t bytes)    { return reinterpret_cast<void*> (bytes); }
}

bool SpriteRenderer::create()
{
    auto& context = *juce::OpenGLContext::getCurrentContext();
    auto program = std::make_unique<juce::OpenGLShaderProgram> (context);

    if (! program->addVertexShader (vertexShader) || ! program->addFragmentShader (fragmentShader) || ! program->link())
    {
        DBG (program->getLastError());
        return false;
    }

    auto backdrop = std::make_unique<juce::OpenGLShaderProgram> (context);

    if (! backdrop->addVertexShader (backdropVertexShader) || ! backdrop->addFragmentShader (backdropFragmentShader) || ! backdrop->link())
    {
        DBG (backdrop->getLastError());
        return false;
    }

    backdropShader = std::move (backdrop);
    const auto backdropId = backdropShader->getProgramID();
    backdropViewport = glGetUniformLocation (backdropId, "uViewport");
    backdropLight = glGetUniformLocation (backdropId, "uLight");
    backdropSphere = glGetUniformLocation (backdropId, "uSphereY");
    backdropShadow = glGetUniformLocation (backdropId, "uShadow");
    backdropHaze = glGetUniformLocation (backdropId, "uHaze");

    shader = std::move (program);
    const auto id = shader->getProgramID();
    viewportUniform = glGetUniformLocation (id, "uViewport");

    GLint previousVertexArray = 0;
    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &previousVertexArray);

    glGenVertexArrays (1, &backdropVertexArray); // empty: the shader makes its own corners
    glGenVertexArrays (1, &vertexArray);
    glBindVertexArray (vertexArray);

    const GLfloat corners[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glGenBuffers (1, &cornerBuffer);
    glBindBuffer (GL_ARRAY_BUFFER, cornerBuffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (corners), corners, GL_STATIC_DRAW);
    const auto corner = (GLuint) glGetAttribLocation (id, "aCorner");
    glEnableVertexAttribArray (corner);
    glVertexAttribPointer (corner, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glGenBuffers (1, &spriteBuffer);
    glBindBuffer (GL_ARRAY_BUFFER, spriteBuffer);

    auto instanceAttribute = [id] (const char* name, GLint size, size_t offset)
    {
        const auto location = (GLuint) glGetAttribLocation (id, name);
        glEnableVertexAttribArray (location);
        glVertexAttribPointer (location, size, GL_FLOAT, GL_FALSE, (GLsizei) sizeof (Sprite), offsetOf (offset));
        glVertexAttribDivisor (location, 1);
    };

    instanceAttribute ("aCentreSize", 3, offsetof (Sprite, x));
    instanceAttribute ("aColour", 3, offsetof (Sprite, r));
    instanceAttribute ("aAlphaProfile", 2, offsetof (Sprite, alpha));

    glBindBuffer (GL_ARRAY_BUFFER, 0);
    glBindVertexArray ((GLuint) previousVertexArray);
    return true;
}

void SpriteRenderer::release()
{
    shader.reset();
    backdropShader.reset();

    if (vertexArray != 0)
    {
        glDeleteVertexArrays (1, &backdropVertexArray);
        glDeleteVertexArrays (1, &vertexArray);
        glDeleteBuffers (1, &cornerBuffer);
        glDeleteBuffers (1, &spriteBuffer);
    }

    vertexArray = cornerBuffer = spriteBuffer = backdropVertexArray = 0;
    bufferBytes = 0;
}

void SpriteRenderer::drawBackdrop (const Backdrop& b, int width, int height)
{
    if (backdropShader == nullptr)
        return;

    GLint previousVertexArray = 0;
    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &previousVertexArray);

    backdropShader->use();
    glUniform2f (backdropViewport, (GLfloat) width, (GLfloat) height);
    glUniform3f (backdropLight, b.centreX, b.lightY, b.radius);
    glUniform1f (backdropSphere, b.sphereY);
    glUniform2f (backdropShadow, b.shadowY, b.shadowHalfWidth);
    glUniform3f (backdropHaze, b.haze[0], b.haze[1], b.haze[2]);

    glDisable (GL_DEPTH_TEST);
    glDisable (GL_BLEND);
    glBindVertexArray (backdropVertexArray);
    glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray ((GLuint) previousVertexArray);
    glUseProgram (0);
}

void SpriteRenderer::draw (const SpriteList& sprites, int width, int height)
{
    if (shader == nullptr || sprites.size() == 0)
        return;

    // JUCE keeps its own vertex array bound for its 2D renderer: put it back afterwards.
    GLint previousVertexArray = 0;
    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &previousVertexArray);

    const auto bytes = (size_t) sprites.size() * sizeof (Sprite);

    glBindVertexArray (vertexArray);
    glBindBuffer (GL_ARRAY_BUFFER, spriteBuffer);
    bufferBytes = std::max (bufferBytes, bytes);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) bufferBytes, nullptr, GL_STREAM_DRAW); // a fresh buffer, so the GPU never makes us wait
    glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) bytes, sprites.data());

    shader->use();
    glUniform2f (viewportUniform, (GLfloat) width, (GLfloat) height);

    glDisable (GL_DEPTH_TEST);
    glEnable (GL_BLEND);
    glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied: alpha covers, alpha 0 adds
    glDrawArraysInstanced (GL_TRIANGLE_STRIP, 0, 4, sprites.size());

    glBindBuffer (GL_ARRAY_BUFFER, 0);
    glBindVertexArray ((GLuint) previousVertexArray);
    glUseProgram (0);
}

} // namespace sphere
