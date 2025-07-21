// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/QtUtils/ClipCursor.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QObject>
#include <QRect>
#include <QWidget>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(HAVE_X11)
#include <X11/Xlib.h>
#endif

namespace QtUtils
{

class CursorClipper final : public QObject
{
public:
  explicit CursorClipper(QWidget* widget) : QObject(widget), m_widget{widget} {}

  bool eventFilter(QObject* obj, QEvent* event) override
  {
    switch (event->type())
    {
    case QEvent::Leave:
    {
      auto pos = QCursor::pos();
      pos.setX(qBound(m_rect.left(), pos.x(), m_rect.right() - 1));
      pos.setY(qBound(m_rect.top(), pos.y(), m_rect.bottom() - 1));
      QCursor::setPos(pos);
      break;
    }
    default:
      break;
    }

    return QObject::eventFilter(obj, event);
  }

  bool Clip(const QRect& rect)
  {
    m_rect = rect;

#ifdef _WIN32
    RECT rect;
    rect.left = rect.left();
    rect.right = rect.right();
    rect.top = rect.top();
    rect.bottom = rect.bottom();

    return ClipCursor(&rect);
#else
    // TODO: Implement on other platforms. XGrabPointer on Linux X11 should be equivalent to
    // ClipCursor on Windows, though XFixesCreatePointerBarrier and XFixesDestroyPointerBarrier
    // may also work. On Wayland zwp_pointer_constraints_v1::confine_pointer and
    // zwp_pointer_constraints_v1::destroy provide this functionality.
    // More info:
    // https://stackoverflow.com/a/36269507
    // https://tronche.com/gui/x/xlib/input/XGrabPointer.html
    // https://www.x.org/releases/X11R7.7/doc/fixesproto/fixesproto.txt
    // https://wayland.app/protocols/pointer-constraints-unstable-v1

    auto* const display = GetX11Display();

    if (display == nullptr)
      return false;

    const auto win = m_widget->winId();

    XGrabPointer(display, win, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, win, None, CurrentTime);

    // parent()->installEventFilter(this);
    return true;
#endif
  }

  static Display* GetX11Display()
  {
    auto* native = qApp->nativeInterface<QNativeInterface::QX11Application>();
    if (native == nullptr)
    {
      qWarning() << "Not running under X11.";
      return nullptr;
    }

    return native->display();
  }

  ~CursorClipper() override
  {
#ifdef _WIN32
    ClipCursor(nullptr);
#else

    auto* const display = GetX11Display();
    if (display != nullptr)
      XUngrabPointer(display, CurrentTime);
#endif
  }

private:
  QWidget* const m_widget;
  QRect m_rect;
};

QObject* ClipCursor(QWidget* widget, const QRect& rect)
{
  auto* const clipper = new CursorClipper{widget};
  if (clipper->Clip(rect))
    return clipper;

  delete clipper;
  return nullptr;
}

}  // namespace QtUtils
