#include "HostWindow.h"

#if defined (__linux__)

#include <X11/Xlib.h>
#include <dlfcn.h>
#include <algorithm>

namespace threedq
{

namespace
{

// JUCE doesn't link X11 either, so this keeps the plugin loadable without it.
struct Xlib
{
    void* library = dlopen ("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);

    template <typename Function>
    Function load (const char* name) const    { return library != nullptr ? reinterpret_cast<Function> (dlsym (library, name)) : nullptr; }

    decltype (&XOpenDisplay) openDisplay = load<decltype (&XOpenDisplay)> ("XOpenDisplay");
    decltype (&XCloseDisplay) closeDisplay = load<decltype (&XCloseDisplay)> ("XCloseDisplay");
    decltype (&XFree) free = load<decltype (&XFree)> ("XFree");
    decltype (&XInternAtom) internAtom = load<decltype (&XInternAtom)> ("XInternAtom");
    decltype (&XQueryTree) queryTree = load<decltype (&XQueryTree)> ("XQueryTree");
    decltype (&XGetWindowProperty) getWindowProperty = load<decltype (&XGetWindowProperty)> ("XGetWindowProperty");
    decltype (&XGetGeometry) getGeometry = load<decltype (&XGetGeometry)> ("XGetGeometry");
    decltype (&XTranslateCoordinates) translateCoordinates = load<decltype (&XTranslateCoordinates)> ("XTranslateCoordinates");
    decltype (&XResizeWindow) resizeWindow = load<decltype (&XResizeWindow)> ("XResizeWindow");

    bool valid() const
    {
        return openDisplay != nullptr && closeDisplay != nullptr && free != nullptr && internAtom != nullptr
            && queryTree != nullptr && getWindowProperty != nullptr && getGeometry != nullptr
            && translateCoordinates != nullptr && resizeWindow != nullptr;
    }
};

bool hasProperty (const Xlib& x, Display* display, Window window, Atom property)
{
    Atom type = None;
    int format = 0;
    unsigned long items = 0, remaining = 0;
    unsigned char* data = nullptr;

    if (x.getWindowProperty (display, window, property, 0, 0, False, AnyPropertyType, &type, &format, &items, &remaining, &data) != Success)
        return false;

    if (data != nullptr)
        x.free (data);

    return type != None;
}

// The application window holding `window`: the outermost ancestor the window
// manager has marked with WM_STATE. Its frame, if it has one, sits above it.
Window findTopLevel (const Xlib& x, Display* display, Window window)
{
    const Atom wmState = x.internAtom (display, "WM_STATE", True);

    if (wmState == None)
        return None;

    Window topLevel = None;

    for (Window current = window; current != None;)
    {
        if (hasProperty (x, display, current, wmState))
            topLevel = current;

        Window root = None, parent = None, *children = nullptr;
        unsigned int count = 0;

        if (x.queryTree (display, current, &root, &parent, &children, &count) == 0)
            return None;

        if (children != nullptr)
            x.free (children);

        current = parent == root ? None : parent;
    }

    return topLevel;
}

} // namespace

void keepHostWindowLargeEnough (void* view, int minWidth, int minHeight)
{
    // How much more than the editor a window may hold and still count as its own.
    constexpr int fillSlack = 16;

    static const Xlib x;

    if (! x.valid() || view == nullptr)
        return;

    auto* display = x.openDisplay (nullptr);

    if (display == nullptr)
        return;

    const auto viewWindow = (Window) view;
    const auto topLevel = findTopLevel (x, display, viewWindow);

    Window root = None, child = None;
    int ignoredX = 0, ignoredY = 0, offsetX = 0, offsetY = 0;
    unsigned int viewW = 0, viewH = 0, topW = 0, topH = 0, border = 0, depth = 0;

    if (topLevel != None
        && x.getGeometry (display, viewWindow, &root, &ignoredX, &ignoredY, &viewW, &viewH, &border, &depth) != 0
        && x.getGeometry (display, topLevel, &root, &ignoredX, &ignoredY, &topW, &topH, &border, &depth) != 0
        && x.translateCoordinates (display, viewWindow, topLevel, 0, 0, &offsetX, &offsetY, &child) != 0
        && (int) topW - offsetX <= (int) viewW + fillSlack
        && (int) topH - offsetY <= (int) viewH + fillSlack)
    {
        const int width = std::max ((int) topW, minWidth + offsetX), height = std::max ((int) topH, minHeight + offsetY);

        if (width != (int) topW || height != (int) topH)
            x.resizeWindow (display, topLevel, (unsigned int) width, (unsigned int) height);
    }

    x.closeDisplay (display); // flushes
}

} // namespace threedq

#else

namespace threedq
{

void keepHostWindowLargeEnough (void*, int, int) {}

} // namespace threedq

#endif
