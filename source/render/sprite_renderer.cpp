/*
  ==============================================================================

    SpriteRenderer: see sprite_renderer.h.

    Each sprite is an instanced quad rather than a GL point: points have a
    driver-specific maximum size and vanish when their centre leaves the
    viewport, which would show up as popping at high zoom.

  ==============================================================================
*/

#include "sprite_renderer.h"
#include "sprite_shaders.h"

using namespace juce::gl;

namespace threedq
{

namespace
{
void* offsetOf (size_t bytes)    { return reinterpret_cast<void*> (bytes); }
}

bool SpriteRenderer::create()
{
    auto& context = *juce::OpenGLContext::getCurrentContext();
    auto program = std::make_unique<juce::OpenGLShaderProgram> (context);

    if (! program->addVertexShader (shaders::sprite) || ! program->addFragmentShader (shaders::spriteFragment) || ! program->link())
    {
        DBG (program->getLastError());
        return false;
    }

    auto backdrop = std::make_unique<juce::OpenGLShaderProgram> (context);

    if (! backdrop->addVertexShader (shaders::backdrop) || ! backdrop->addFragmentShader (shaders::backdropFragment) || ! backdrop->link())
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

} // namespace threedq
