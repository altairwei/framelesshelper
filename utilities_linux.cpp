/*
 * MIT License
 *
 * Copyright (C) 2021 by wangwenx190 (Yuhang Zhao)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "utilities.h"
#include <QtGui/qwindow.h>
#include <QtGui/qpa/qplatformnativeinterface.h>
#include <QtGui/qscreen.h>
#include <QtPlatformHeaders/QXcbScreenFunctions> // Qt style include
#include <X11/Xlib.h>

// Build system
#define FRAMELESSHELPER_HAS_X11

#ifndef _NET_WM_MOVERESIZE_SIZE_TOPLEFT
#define _NET_WM_MOVERESIZE_SIZE_TOPLEFT      0
#endif

#ifndef _NET_WM_MOVERESIZE_SIZE_TOP
#define _NET_WM_MOVERESIZE_SIZE_TOP          1
#endif

#ifndef _NET_WM_MOVERESIZE_SIZE_TOPRIGHT
#define _NET_WM_MOVERESIZE_SIZE_TOPRIGHT     2
#endif

#ifndef _NET_WM_MOVERESIZE_SIZE_RIGHT
#define _NET_WM_MOVERESIZE_SIZE_RIGHT        3
#endif

#ifndef _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT  4
#endif

#ifndef _NET_WM_MOVERESIZE_SIZE_BOTTOM
#define _NET_WM_MOVERESIZE_SIZE_BOTTOM       5
#endif

#ifndef _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT   6
#endif

#ifndef _NET_WM_MOVERESIZE_SIZE_LEFT
#define _NET_WM_MOVERESIZE_SIZE_LEFT         7
#endif

#ifndef _NET_WM_MOVERESIZE_MOVE
#define _NET_WM_MOVERESIZE_MOVE              8   // movement only
#endif

#ifndef _NET_WM_MOVERESIZE_SIZE_KEYBOARD
#define _NET_WM_MOVERESIZE_SIZE_KEYBOARD     9   // size via keyboard
#endif

#ifndef _NET_WM_MOVERESIZE_MOVE_KEYBOARD
#define _NET_WM_MOVERESIZE_MOVE_KEYBOARD    10   // move via keyboard
#endif

#ifndef _NET_WM_MOVERESIZE_CANCEL
#define _NET_WM_MOVERESIZE_CANCEL           11   // cancel operation
#endif

#ifdef FRAMELESSHELPER_HAS_X11
static inline Display *x11_get_display()
{
    if (!qApp) {
        return nullptr;
    }
    QPlatformNativeInterface *native = qApp->platformNativeInterface();
    if (!native) {
        return nullptr;
    }
    return reinterpret_cast<Display *>(native->nativeResourceForIntegration(QByteArray("display")));
}

static inline int x11_get_x11screen()
{
    if (!qApp) {
        return 0;
    }
    QPlatformNativeInterface *native = qApp->platformNativeInterface();
    if (!native) {
        return 0;
    }
    return reinterpret_cast<qintptr>(native->nativeResourceForIntegration(QByteArrayLiteral("x11screen")));
}

static inline QScreen *x11_findScreenForVirtualDesktop(const int virtualDesktopNumber)
{
    if (virtualDesktopNumber == -1) {
        return QGuiApplication::primaryScreen();
    }
    const auto screens = QGuiApplication::screens();
    for (auto &&screen : qAsConst(screens)) {
        if (QXcbScreenFunctions::virtualDesktopNumber(screen) == virtualDesktopNumber) {
            return screen;
        }
    }
    return nullptr;
}

// unsigned long
static inline quint64 x11_get_rootWindow(const int screen)
{
    if (!qApp) {
        return 0;
    }
    QPlatformNativeInterface *native = qApp->platformNativeInterface();
    if (!native) {
        return 0;
    }
    QScreen *screen = x11_findScreenForVirtualDesktop(screen);
    if (!screen) {
        return 0;
    }
    return static_cast<quint64>(reinterpret_cast<quintptr>(native->nativeResourceForScreen(QByteArrayLiteral("rootwindow"), screen)));
}

static inline void x11_emulateButtonRelease(const QWindow *win, const QPoint &pos)
{
    Q_ASSERT(win);
    if (!win) {
        return;
    }
    Q_ASSERT(!pos.isNull());
    if (pos.isNull()) {
        return;
    }
    const QPoint clientPos = win->mapFromGlobal(pos);
    Display *display = x11_get_display(); // try const
    Window window = win->winId(); // try const
    XEvent event;
    memset(&event, 0, sizeof(event));
    //event.xbutton.button = 0;
    event.xbutton.same_screen = True; // or true?
    event.xbutton.send_event = True; // or true?
    event.xbutton.window = window;
    event.xbutton.root = x11_get_rootWindow(x11_get_x11screen());
    event.xbutton.x_root = pos.x();
    event.xbutton.y_root = pos.y();
    event.xbutton.x = clientPos.x();
    event.xbutton.y = clientPos.y();
    event.xbutton.type = ButtonRelease; // constant or macro?
    event.xbutton.time = CurrentTime; // constant or macro?
    if (XSendEvent(display, window, True, ButtonReleaseMask, &event) == 0) {
        qWarning() << "Cant send ButtonRelease for native drag";
    }
    XFlush(display);
}

static inline void x11_wmMoveResizeWindow(const QWindow *win, const int x, const int y, const int section)
{
    Display *display = x11_get_display(); // try const
    static Atom netMoveResize = XInternAtom(display, "_NET_WM_MOVERESIZE", False); // try const

    // First we need to ungrab the pointer that may have been automatically grabbed by Qt on ButtonPressEvent.
    XUngrabPointer(display, CurrentTime); // function result?

    XEvent event;
    memset(&event, /*0x00*/0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.window = win->winId();
    event.xclient.message_type = netMoveResize;
    //event.xclient.serial = 0;
    event.xclient.display = display;
    event.xclient.send_event = True;
    event.xclient.format = 32;
    event.xclient.data.l[0] = x;
    event.xclient.data.l[1] = y;
    event.xclient.data.l[2] = section;
    event.xclient.data.l[3] = Button1;
    //event.xclient.data.l[4] = 0; /* unused */
    if (XSendEvent(display, x11_get_rootWindow(x11_get_x11screen()), False, SubstructureRedirectMask | SubstructureNotifyMask, &event) == 0) {
        qWarning() << "Cant send _NET_WM_MOVERESIZE for native drag";
    }
    XFlush(display);
}
#endif

Qt::MouseButtons flh_get_mouseButtons_linux()
{
    Qt::MouseButtons result = {};
#ifdef FRAMELESSHELPER_HAS_X11
    const bool mouseSwapped = false; // make use of it
    Display *display = x11_get_display(); // try const
    Window w, r = x11_get_rootWindow(x11_get_x11screen()); // w? r? const?
    int wx = 0;
    int wy = 0;
    int rx = 0;
    int ry = 0;
    quint32 m = 0;
    XQueryPointer(display, r, &r, &w, &rx, &ry, &wx, &wy, &m); // function result?
    if (m & Button1Mask) {
        result |= (mouseSwapped ? Qt::RightButton : Qt::LeftButton);
    }
    if (m & Button2Mask) {
        result |= Qt::MidButton;
    }
    if (m & Button3Mask) {
        result |= (mouseSwapped ? Qt::LeftButton : Qt::RightButton);
    }
    if (m & Button4Mask) {
        result |= Qt::XButton1;
    }
    if (m & Button5Mask) {
        result |= Qt::XButton2;
    }
#endif
    // TODO: what about wayland?
    return result;
}

QRect flh_get_window_geometry_linux(const QWindow *win)
{
    QRect result = {};
#ifdef FRAMELESSHELPER_HAS_X11
    Display *display = x11_get_display(); // try const
    Window root_window = x11_get_rootWindow(x11_get_x11screen()); // try const
    Window window = win->winId();
    Window child_window; // initial value
    XWindowAttributes attrs; // initial value
    int x = 0;
    int y = 0;
    XTranslateCoordinates(display, window, root_window, 0, 0, &x, &y, &child_window); // function result?
    Status status = XGetWindowAttributes(display, window, &attrs); // try const
    if (status != 0) {
        result = {x - attrs.x, y - attrs.y, attrs.width, attrs.height};
    }
#endif
    // TODO: what about wayland?
    return result;
}

// Use Qt::Edges instead of Qt::WindowFrameSection ?
bool flh_window_start_native_drag_linux(const QWindow *win, const QPoint &pos, const Qt::WindowFrameSection frameSection)
{
    int section = -1;
    switch (frameSection) {
    case Qt::LeftSection:
        section = _NET_WM_MOVERESIZE_SIZE_LEFT;
        break;
    case Qt::TopLeftSection:
        section = _NET_WM_MOVERESIZE_SIZE_TOPLEFT;
        break;
    case Qt::TopSection:
        section = _NET_WM_MOVERESIZE_SIZE_TOP;
        break;
    case Qt::TopRightSection:
        section = _NET_WM_MOVERESIZE_SIZE_TOPRIGHT;
        break;
    case Qt::RightSection:
        section = _NET_WM_MOVERESIZE_SIZE_RIGHT;
        break;
    case Qt::BottomRightSection:
        section = _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT;
        break;
    case Qt::BottomSection:
        section = _NET_WM_MOVERESIZE_SIZE_BOTTOM;
        break;
    case Qt::BottomLeftSection:
        section = _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT;
        break;
    case Qt::TitleBarArea:
        section = _NET_WM_MOVERESIZE_MOVE;
        break;
    default:
        break;
    }
    if (section != -1) {
#ifdef FRAMELESSHELPER_HAS_X11
        // Before start the drag we need to tell Qt that the mouse is Released!
        x11_emulateButtonRelease(win, pos);
        x11_wmMoveResizeWindow(win, pos.x(), pos.y(), section);
#endif
    }
    return true;
}
