// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/QtUtils/ClipCursor.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QObject>
#include <QWindow>
#include <QScreen>
#include <QRect>
#include <QWidget>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(HAVE_X11)
#include <X11/Xlib.h>
#endif

#include "InputCommon/ControllerInterface/ControllerInterface.h"

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
      const auto center = m_widget->rect().center();
      const auto global_center = m_widget->mapToGlobal(center);
      QCursor::setPos(global_center);
      break;
    }
    default:
      break;
    }

    return QObject::eventFilter(obj, event);
  }

#ifdef _WIN32
  bool Clip()
  {
    // It seems like QT doesn't scale the window frame correctly with some DPIs
    // so it might happen that the locked cursor can be on the frame of the window,
    // being able to resize it, but that is a minor problem.
    // As a hack, if necessary, we could always scale down the size by 2 pixel, to a min of 1 given
    // that the size can be 0 already. We probably shouldn't scale axes already scaled by aspect
    // ratio
    QRect render_rect = m_widget->geometry();
    if (m_widget->parentWidget())
    {
      //render_rect.moveTopLeft((m_widget->window()->parentWidget()->mapToGlobal(render_rect.topLeft()));
    }
    auto scale =
        m_widget->devicePixelRatioF();  // Seems to always be rounded on Win. Should we round results?
    QPoint screen_offset = QPoint(0, 0);

    const auto window_handle = m_widget->window()->windowHandle();
    if (window_handle && window_handle->screen())
    {
        screen_offset = window_handle->screen()->geometry().topLeft();
    }
    render_rect.moveTopLeft(((render_rect.topLeft() - screen_offset) * scale) + screen_offset);
    render_rect.setSize(render_rect.size() * scale);

    constexpr bool follow_aspect_ratio = true;
    if (follow_aspect_ratio)
    {
      // TODO: SetCursorLocked() should be re-called every time this value is changed?
      // This might cause imprecisions of one pixel (but it won't cause the cursor to go over
      // borders)
      Common::Vec2 aspect_ratio = g_controller_interface.GetWindowInputScale();
      if (aspect_ratio.x > 1.f)
      {
        const float new_half_width = float(render_rect.width()) / (aspect_ratio.x * 2.f);
        // Only ceil if it was >= 0.25
        const float ceiled_new_half_width = std::ceil(std::round(new_half_width * 2.f) / 2.f);
        const int x_center = render_rect.center().x();
        // Make a guess on which one to floor and ceil.
        // For more precision, we should have kept the rounding point scale from above as well.
        render_rect.setLeft(x_center - std::floor(new_half_width));
        render_rect.setRight(x_center + ceiled_new_half_width);
      }
      if (aspect_ratio.y > 1.f)
      {
        const float new_half_height = render_rect.height() / (aspect_ratio.y * 2.f);
        const float ceiled_new_half_height = std::ceil(std::round(new_half_height * 2.f) / 2.f);
        const int y_center = render_rect.center().y();
        render_rect.setTop(y_center - std::floor(new_half_height));
        render_rect.setBottom(y_center + ceiled_new_half_height);
      }
    }

    RECT rect;
    rect.left = render_rect.left();
    rect.right = render_rect.right();
    rect.top = render_rect.top();
    rect.bottom = render_rect.bottom();

    return ::ClipCursor(&rect);
  }
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
#else
  bool Clip()
  {
    parent()->installEventFilter(this);
    return true;
  }
#endif

  ~CursorClipper() override
  {
#ifdef _WIN32
    ::ClipCursor(nullptr);
#elif defined(HAVE_X11)
    auto* const display = GetX11Display();
    if (display != nullptr)
      XUngrabPointer(display, CurrentTime);
#else
    parent()->removeEventFilter(this);
#endif
  }

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
