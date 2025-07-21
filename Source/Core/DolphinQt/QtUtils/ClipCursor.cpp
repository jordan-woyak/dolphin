// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/QtUtils/ClipCursor.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QObject>
#include <QRect>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(HAVE_X11)
#include <X11/Xlib.h>
#endif

#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "Common/Logging/Log.h"

namespace QtUtils
{

class CursorClipper final : public QObject
{
public:
  explicit CursorClipper(QWidget* widget) : QObject(widget), m_widget{widget} {}

#if defined(_WIN32)
  bool eventFilter(QObject* obj, QEvent* event) override
  {
    switch (event->type())
    {
    // TODO: also handle parent move.
    case QEvent::Move:
    case QEvent::Resize:
    // TODO: these needed?
    case QEvent::ParentChange:
    case QEvent::WinIdChange:
    case QEvent::WindowStateChange:
      Clip();
      break;
    default:
      break;
    }

    return QObject::eventFilter(obj, event);
  }

  bool Clip()
  {
    RECT rect;
    if (GetWindowRect(reinterpret_cast<HWND>(m_widget->winId()), &rect) && ClipCursor(&rect))
    {
      parent()->installEventFilter(this);
      return true;
    }

    return false;
  }

  ~CursorClipper() override { ::ClipCursor(nullptr); }

#elif defined(HAVE_X11)
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

  bool Clip()
  {
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

    return true;
  }

  ~CursorClipper() override
  {
    auto* const display = GetX11Display();
    if (display != nullptr)
      XUngrabPointer(display, CurrentTime);
  }

#else
  bool eventFilter(QObject* obj, QEvent* event) override
  {
    switch (event->type())
    {
    case QEvent::Leave:
    {
      auto rect = m_widget->rect();
      rect.moveTopLeft(m_widget->mapToGlobal(rect.topLeft()));

      auto cursor_pos = QCursor::pos();
      INFO_LOG_FMT(VIDEO, "cursor x:{} y:{}", cursor_pos.x(), cursor_pos.y());

      cursor_pos.setX(qBound(rect.left(), cursor_pos.x(), rect.right()));
      cursor_pos.setY(qBound(rect.top(), cursor_pos.y(), rect.bottom()));

      INFO_LOG_FMT(VIDEO, "adjusted cursor x:{} y:{}", cursor_pos.x(), cursor_pos.y());
      // TODO: why doesn't it work.. virtualbox related?
      QCursor::setPos(cursor_pos);
      break;
    }
    default:
      break;
    }

    return QObject::eventFilter(obj, event);
  }

  bool Clip()
  {
    parent()->installEventFilter(this);
    return true;
  }
#endif

private:
  QWidget* const m_widget;
};

QObject* ClipCursor(QWidget* widget)
{
  auto* const clipper = new CursorClipper{widget};
  if (clipper->Clip())
    return clipper;

  delete clipper;
  return nullptr;
}

}  // namespace QtUtils
