/* SPDX-License-Identifier: MIT */
/* Keep Xlib types separate from the X server's internal headers. */
#include "hostcursor.h"
#ifdef HAVE_HOST_XFIXES
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

static Display *hostDisplay;
static Window hostWindow;
static Bool cursorHidden;

static void
setHidden(Bool hidden)
{
    if (!hostWindow || hidden == cursorHidden)
        return;
    if (hidden)
        XFixesHideCursor(hostDisplay, hostWindow);
    else
        XFixesShowCursor(hostDisplay, hostWindow);
    cursorHidden = hidden;
    XFlush(hostDisplay);
}

static int
ignoreError(Display *display, XErrorEvent *event)
{
    return 0;
}

int
sixelHostCursorInit(void)
{
    const char *id = getenv("WINDOWID");
    const char *display = getenv("DISPLAY");
    char *end;
    unsigned long window;
    int major = 4, minor = 0;
    XWindowAttributes attr;
    int (*previousHandler)(Display *, XErrorEvent *);
    Window root, child;
    int rx, ry, wx, wy;
    unsigned int mask;
    Bool valid;

    if (!id || !display || !*display)
        return -1;
    errno = 0;
    window = strtoul(id, &end, 0);
    if (errno || end == id || *end || !window)
        return -1;
    hostDisplay = XOpenDisplay(display);
    if (!hostDisplay)
        return -1;
    if (!XFixesQueryVersion(hostDisplay, &major, &minor) || major < 4)
        goto fail;
    previousHandler = XSetErrorHandler(ignoreError);
    valid = XGetWindowAttributes(hostDisplay, window, &attr);
    XSync(hostDisplay, False);
    XSetErrorHandler(previousHandler);
    if (!valid)
        goto fail;
    hostWindow = window;
    XSelectInput(hostDisplay, hostWindow, EnterWindowMask | LeaveWindowMask | StructureNotifyMask);
    if (XQueryPointer(hostDisplay, hostWindow, &root, &child, &rx, &ry,
                      &wx, &wy, &mask))
        setHidden(wx >= 0 && wy >= 0 && wx < attr.width && wy < attr.height);
    XFlush(hostDisplay);
    fcntl(ConnectionNumber(hostDisplay), F_SETFD, FD_CLOEXEC);
    return ConnectionNumber(hostDisplay);
fail:
    XCloseDisplay(hostDisplay);
    hostDisplay = NULL;
    return -1;
}

void
sixelHostCursorDispatch(void)
{
    while (hostDisplay && XPending(hostDisplay)) {
        XEvent event;
        XNextEvent(hostDisplay, &event);
        if (event.type == DestroyNotify) {
            /* Window destruction releases its XFixes hide request. */
            hostWindow = None;
            cursorHidden = False;
        } else if ((event.type == EnterNotify || event.type == LeaveNotify) &&
                   event.xcrossing.detail != NotifyInferior &&
                   event.xcrossing.mode == NotifyNormal) {
            setHidden(event.type == EnterNotify);
        }
    }
}

void
sixelHostCursorFini(void)
{
    if (hostDisplay) {
        setHidden(False);
        XCloseDisplay(hostDisplay);
        hostDisplay = NULL;
    }
    hostWindow = None;
    cursorHidden = False;
}
#else
int sixelHostCursorInit(void) { return -1; }
void sixelHostCursorDispatch(void) { }
void sixelHostCursorFini(void) { }
#endif
