/*
  ==============================================================================

    Grows a host's window back when it has been made too small for the editor.

    REAPER on Linux lets its plugin windows shrink to 20x20 whatever limits the
    editor sets, and the editor then just gets cropped. Window manager size
    hints can't stop that: REAPER resets them on every resize.

  ==============================================================================
*/

#pragma once

namespace threedq
{

// `view` is the editor's native window; the sizes are the editor's minimum, in
// physical pixels. Room for the host's own controls is added on top. Call it
// regularly. Does nothing when the editor doesn't fill its window (docked, for
// instance).
void keepHostWindowLargeEnough (void* view, int minWidth, int minHeight);

} // namespace threedq
