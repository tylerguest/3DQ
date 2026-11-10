/*
  ==============================================================================

    What the renderer draws: a list of glowing dots in device pixels, and the
    backdrop behind them. Plain data, so the particle maths never touches the
    graphics API.

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <array>
#include <vector>

namespace threedq
{

// One glowing dot, in device pixels. The layout is what the GPU receives.
struct Sprite
{
    float x, y, size;
    float r, g, b;
    float alpha;
    float profile; // 0 = particle (crisp core + halo), 1 = tint (soft falloff)
};

// What's drawn behind the sphere, in device pixels (y down).
struct Backdrop
{
    float centreX = 0, lightY = 0; // the middle of the shape as it looks
    float sphereY = 0, radius = 1; // the sphere's own centre and size
    float shadowY = 0, shadowHalfWidth = 1;
    std::array<float, 3> haze {}; // 0..1 lows, mids, highs
};

struct SpriteStyle
{
    float r, g, b, profile;
};

class SpriteList
{
public:
    void clear()                     { count = 0; }
    const Sprite* data() const       { return sprites.data(); }
    int size() const                 { return count; }

    void add (const SpriteStyle& s, float x, float y, float size, float alpha)
    {
        if (alpha <= 0.003f)
            return;

        if ((size_t) count == sprites.size())
            sprites.resize (std::max<size_t> (32768, sprites.size() * 2));

        sprites[(size_t) count++] = { x, y, size, s.r, s.g, s.b, std::min (alpha, 1.0f), s.profile };
    }

private:
    std::vector<Sprite> sprites;
    int count = 0;
};

} // namespace threedq
