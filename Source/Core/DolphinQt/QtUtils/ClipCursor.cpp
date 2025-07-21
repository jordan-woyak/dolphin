// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/QtUtils/ClipCursor.h"

#include <QCursor>
#include <QEvent>
#include <QObject>
#include <QRect>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "Common/Logging/Log.h"

namespace QtUtils
{

class CursorClipper final : public QObject
{
public:
  using QObject::QObject;

  bool event(QEvent* event) override
  {
    INFO_LOG_FMT(VIDEO, "QEvent");

    switch (event->type())
    {
    case QEvent::Leave:
    {
      INFO_LOG_FMT(VIDEO, "QEvent::Leave");
      auto pos = QCursor::pos();
      pos.setX(qBound(m_rect.left(), pos.x(), m_rect.right() - 1));
      pos.setY(qBound(m_rect.top(), pos.y(), m_rect.bottom() - 1));
      QCursor::setPos(pos);
      break;
    }
    default:
      break;
    }

    return QObject::event(event);
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

    parent()->installEventFilter(this);
    return true;
#endif
  }

  ~CursorClipper() override
  {
#ifdef _WIN32
    ClipCursor(nullptr);
#else

#endif
  }

private:
  QRect m_rect;
};

QObject* ClipCursor(QObject* obj, const QRect& rect)
{
  auto* const clipper = new CursorClipper{obj};
  if (clipper->Clip(rect))
    return clipper;

  delete clipper;
  return nullptr;
}

}  // namespace QtUtils
