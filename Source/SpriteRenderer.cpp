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
out float vAlpha;
out float vProfile;
out float vSize;

void main()
{
    vec2 p = aCentreSize.xy + aCorner * aCentreSize.z * 0.5;
    gl_Position = vec4 (p.x / uViewport.x * 2.0 - 1.0, 1.0 - p.y / uViewport.y * 2.0, 0.0, 1.0);
    vCoord = aCorner;
    vColour = aColour;
    vAlpha = aAlphaProfile.x;
    vProfile = aAlphaProfile.y;
    vSize = aCentreSize.z;
}
)";

// Glow profiles (t = 0 centre .. 1 edge), as premultiplied colour for additive blending.
const char* const fragmentShader = R"(#version 150
in vec2 vCoord;
in vec3 vColour;
in float vAlpha;
in float vProfile;
in float vSize;
out vec4 fragColour;

void main()
{
    float t = length (vCoord);
    if (t >= 1.0)
        discard;

    vec3 rgb;
    float a;

    if (vProfile < 0.5)
    {
        // Particle: a crisp, anti-aliased core (about one pixel of edge
        // softness whatever the sprite size) inside a faint halo.
        float px = 2.0 / max (vSize, 1.0);
        float coreR = 0.3;
        float core = 1.0 - smoothstep (coreR - px, coreR + px, t);
        float halo = 0.22 * pow (1.0 - t, 2.0);
        rgb = mix (vColour, vec3 (1.0), 0.35 * (1.0 - smoothstep (0.0, coreR, t)));
        a = max (core, halo);
    }
    else
    {
        rgb = vColour;
        a = t < 0.4 ? mix (1.0, 0.35, t / 0.4) : mix (0.35, 0.0, (t - 0.4) / 0.6);
    }

    a *= vAlpha;
    fragColour = vec4 (rgb * a, a);
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

    shader = std::move (program);
    const auto id = shader->getProgramID();
    viewportUniform = glGetUniformLocation (id, "uViewport");

    GLint previousVertexArray = 0;
    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &previousVertexArray);

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

    if (vertexArray != 0)
    {
        glDeleteVertexArrays (1, &vertexArray);
        glDeleteBuffers (1, &cornerBuffer);
        glDeleteBuffers (1, &spriteBuffer);
    }

    vertexArray = cornerBuffer = spriteBuffer = 0;
    bufferBytes = 0;
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
    glBlendFunc (GL_ONE, GL_ONE);
    glDrawArraysInstanced (GL_TRIANGLE_STRIP, 0, 4, sprites.size());
    glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer (GL_ARRAY_BUFFER, 0);
    glBindVertexArray ((GLuint) previousVertexArray);
    glUseProgram (0);
}

} // namespace sphere
