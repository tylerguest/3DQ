/*
  ==============================================================================

    SpriteRenderer: draws a SpriteList with OpenGL. This is the only file that
    draws particles through a graphics API; a Vulkan or Metal backend would
    replace this class and leave the particle maths alone.

    Every method must be called with the OpenGL context active.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Sprites.h"

namespace threedq
{

class SpriteRenderer
{
public:
    bool create();
    void release();
    void draw (const SpriteList& sprites, int width, int height);

    // Fills the frame; call before draw().
    void drawBackdrop (const Backdrop& backdrop, int width, int height);

private:
    std::unique_ptr<juce::OpenGLShaderProgram> backdropShader;
    juce::uint32 backdropVertexArray = 0;
    int backdropViewport = -1, backdropLight = -1, backdropSphere = -1, backdropShadow = -1, backdropHaze = -1;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    juce::uint32 vertexArray = 0, cornerBuffer = 0, spriteBuffer = 0;
    int viewportUniform = -1;
    size_t bufferBytes = 0;
};

} // namespace threedq
