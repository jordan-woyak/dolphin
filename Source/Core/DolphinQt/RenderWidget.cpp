// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/RenderWidget.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/State.h"
#include "Core/System.h"

#include "DolphinQt/Host.h"
#include "DolphinQt/QtUtils/ClipCursor.h"
#include "DolphinQt/QtUtils/ModalMessageBox.h"
#include "DolphinQt/Resources.h"
#include "DolphinQt/Settings.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"

#include "VideoCommon/Present.h"

#ifdef _WIN32
#include <dwmapi.h>
#endif

RenderWidget::RenderWidget(QWidget* parent) : QWidget(parent)
{
  setWindowTitle(QStringLiteral("Dolphin"));
  setWindowIcon(Resources::GetAppIcon());
  setWindowRole(QStringLiteral("renderer"));
  setAcceptDrops(true);

  QPalette p;
  p.setColor(QPalette::Window, Qt::black);
  setPalette(p);

  connect(Host::GetInstance(), &Host::RequestTitle, this, &RenderWidget::setWindowTitle);
  connect(Host::GetInstance(), &Host::RequestRenderSize, this, [this](int w, int h) {
    if (!Config::Get(Config::MAIN_RENDER_WINDOW_AUTOSIZE) || isFullScreen() || isMaximized())
      return;

    const auto dpr = window()->windowHandle()->screen()->devicePixelRatio();

    resize(w / dpr, h / dpr);
  });

  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this, [this](Core::State state) {
    if (state == Core::State::Running)
      SetPresenterKeyMap();
  });

  // We have to use Qt::DirectConnection here because we don't want those signals to get queued
  // (which results in them not getting called)
  connect(this, &RenderWidget::StateChanged, Host::GetInstance(), &Host::SetRenderFullscreen,
          Qt::DirectConnection);
  connect(this, &RenderWidget::HandleChanged, this, &RenderWidget::OnHandleChanged,
          Qt::DirectConnection);
  connect(this, &RenderWidget::SizeChanged, Host::GetInstance(), &Host::ResizeSurface,
          Qt::DirectConnection);
  connect(this, &RenderWidget::FocusChanged, Host::GetInstance(), &Host::SetRenderFocus,
          Qt::DirectConnection);

  m_mouse_timer = new QTimer(this);
  connect(m_mouse_timer, &QTimer::timeout, this, &RenderWidget::HandleCursorTimer);
  m_mouse_timer->setSingleShot(true);
  setMouseTracking(true);

  connect(&Settings::Instance(), &Settings::CursorVisibilityChanged, this,
          &RenderWidget::OnHideCursorChanged);
  connect(&Settings::Instance(), &Settings::LockCursorChanged, this,
          &RenderWidget::OnLockCursorChanged);
  OnHideCursorChanged();
  OnLockCursorChanged();
  connect(&Settings::Instance(), &Settings::KeepWindowOnTopChanged, this,
          &RenderWidget::OnKeepOnTopChanged);
  OnKeepOnTopChanged(Settings::Instance().IsKeepWindowOnTopEnabled());
  m_mouse_timer->start(MOUSE_HIDE_DELAY);

  // We need a native window to render into.
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_PaintOnScreen);
}

QPaintEngine* RenderWidget::paintEngine() const
{
  return nullptr;
}

void RenderWidget::dragEnterEvent(QDragEnterEvent* event)
{
  if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1)
    event->acceptProposedAction();
}

void RenderWidget::dropEvent(QDropEvent* event)
{
  const auto& urls = event->mimeData()->urls();
  if (urls.empty())
    return;

  const auto& url = urls[0];
  QFileInfo file_info(url.toLocalFile());

  auto path = file_info.filePath();

  if (!file_info.exists() || !file_info.isReadable())
  {
    ModalMessageBox::critical(this, tr("Error"), tr("Failed to open '%1'").arg(path));
    return;
  }

  if (!file_info.isFile())
  {
    return;
  }

  State::LoadAs(Core::System::GetInstance(), path.toStdString());
}

void RenderWidget::OnHandleChanged(void* handle)
{
  if (handle)
  {
#ifdef _WIN32
    // Remove rounded corners from the render window on Windows 11
    const DWM_WINDOW_CORNER_PREFERENCE corner_preference = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(static_cast<HWND>(handle), DWMWA_WINDOW_CORNER_PREFERENCE,
                          &corner_preference, sizeof(corner_preference));
#endif
  }
  Host::GetInstance()->SetRenderHandle(handle);
}

void RenderWidget::OnHideCursorChanged()
{
  UpdateCursor();
}

void RenderWidget::OnLockCursorChanged()
{
  SetCursorLocked(false);
  UpdateCursor();
}

// Calling this at any time will set the cursor (image) to the correct state
void RenderWidget::UpdateCursor()
{
  if (!Settings::Instance().GetLockCursor())
  {
    // Only hide if the cursor is automatically locking (it will hide on lock).
    // "Unhide" the cursor if we lost focus, otherwise it will disappear when hovering
    // on top of the game window in the background
    const bool keep_on_top = (windowFlags() & Qt::WindowStaysOnTopHint) != 0;
    const bool should_hide =
        (Settings::Instance().GetCursorVisibility() == Config::ShowCursor::Never) &&
        (keep_on_top || Config::Get(Config::MAIN_INPUT_BACKGROUND_INPUT) || isActiveWindow());
    setCursor(should_hide ? Qt::BlankCursor : Qt::ArrowCursor);
  }
  else
  {
    setCursor((IsCursorLocked() &&
               Settings::Instance().GetCursorVisibility() == Config::ShowCursor::Never) ?
                  Qt::BlankCursor :
                  Qt::ArrowCursor);
  }
}

void RenderWidget::OnKeepOnTopChanged(bool top)
{
  const bool was_visible = isVisible();

  setWindowFlags(top ? windowFlags() | Qt::WindowStaysOnTopHint :
                       windowFlags() & ~Qt::WindowStaysOnTopHint);

  m_dont_lock_cursor_on_show = true;
  if (was_visible)
    show();
  m_dont_lock_cursor_on_show = false;

  UpdateCursor();
}

void RenderWidget::HandleCursorTimer()
{
  if (!isActiveWindow())
    return;
  if ((!Settings::Instance().GetLockCursor() || IsCursorLocked()) &&
      Settings::Instance().GetCursorVisibility() == Config::ShowCursor::OnMovement)
  {
    setCursor(Qt::BlankCursor);
  }
}

void RenderWidget::showFullScreen()
{
  QWidget::showFullScreen();

  QScreen* screen = window()->windowHandle()->screen();

  const auto dpr = screen->devicePixelRatio();

  emit SizeChanged(width() * dpr, height() * dpr);
}

// Lock the cursor within the window/widget internal borders, including the aspect ratio if wanted
void RenderWidget::SetCursorLocked(bool locked, bool follow_aspect_ratio)
{

  if (locked)
  {
    INFO_LOG_FMT(VIDEO, "lock cursor");
    delete std::exchange(m_cursor_clipper, nullptr);
    m_cursor_clipper = QtUtils::ClipCursor(this);
    if (m_cursor_clipper != nullptr)
    {
      if (Settings::Instance().GetCursorVisibility() != Config::ShowCursor::Constantly)
      {
        setCursor(Qt::BlankCursor);
      }

      Host::GetInstance()->SetRenderFullFocus(true);
    }
  }
  else
  {
    if (!IsCursorLocked())
      return;

    INFO_LOG_FMT(VIDEO, "unlock cursor");

    delete std::exchange(m_cursor_clipper, nullptr);

    // Center the mouse in the window if it's still active
    // Leave it where it was otherwise, e.g. a prompt has opened or we alt tabbed.
    if (isActiveWindow())
    {
      const auto center = rect().center();
      const auto global_center = mapToGlobal(center);
      QCursor::setPos(global_center);
    }

    // Show the cursor so the user knows it has been unlocked.
    setCursor(Qt::ArrowCursor);

    Host::GetInstance()->SetRenderFullFocus(false);
  }
}

bool RenderWidget::IsCursorLocked() const
{
  return m_cursor_clipper != nullptr;
}

void RenderWidget::SetCursorLockedOnNextActivation(bool locked)
{
  if (Settings::Instance().GetLockCursor())
  {
    m_lock_cursor_on_next_activation = locked;
    return;
  }
  m_lock_cursor_on_next_activation = false;
}

void RenderWidget::SetWaitingForMessageBox(bool waiting_for_message_box)
{
  if (m_waiting_for_message_box == waiting_for_message_box)
  {
    return;
  }
  m_waiting_for_message_box = waiting_for_message_box;
  if (!m_waiting_for_message_box && m_lock_cursor_on_next_activation && isActiveWindow())
  {
    if (Settings::Instance().GetLockCursor())
    {
      SetCursorLocked(true);
    }
    m_lock_cursor_on_next_activation = false;
  }
}

bool RenderWidget::event(QEvent* event)
{
  PassEventToPresenter(event);

  switch (event->type())
  {
  case QEvent::KeyPress:
  {
    QKeyEvent* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Escape)
      emit EscapePressed();

    // The render window might flicker on some platforms because Qt tries to change focus to a new
    // element when there is none (?) Handling this event before it reaches QWidget fixes the issue.
    if (ke->key() == Qt::Key_Tab)
      return true;

    break;
  }
  // Needed in case a new window open and it moves the mouse
  case QEvent::WindowBlocked:
    SetCursorLocked(false);
    break;
  case QEvent::MouseButtonPress:

    // Grab focus to stop unwanted keyboard input UI interaction.
    setFocus();

    if (isActiveWindow())
    {
      // Lock the cursor with any mouse button click (behave the same as window focus change).
      // This event is occasionally missed because isActiveWindow is laggy
      if (Settings::Instance().GetLockCursor())
      {
        SetCursorLocked(true);
      }
    }
    break;
  case QEvent::MouseMove:
    // Unhide on movement
    if (Settings::Instance().GetCursorVisibility() == Config::ShowCursor::OnMovement)
    {
      setCursor(Qt::ArrowCursor);
      m_mouse_timer->start(MOUSE_HIDE_DELAY);
    }
    break;
  case QEvent::WinIdChange:
    emit HandleChanged(reinterpret_cast<void*>(winId()));
    break;
  case QEvent::Show:
    // Don't do if "stay on top" changed (or was true)
    if (Settings::Instance().GetLockCursor() &&
        Settings::Instance().GetCursorVisibility() != Config::ShowCursor::Constantly &&
        !m_dont_lock_cursor_on_show)
    {
      // Auto lock when this window is shown (it was hidden)
      if (isActiveWindow())
        SetCursorLocked(true);
      else
        SetCursorLockedOnNextActivation();
    }
    break;
  // Note that this event in Windows is not always aligned to the window that is highlighted,
  // it's the window that has keyboard and mouse focus
  case QEvent::WindowActivate:
    if (m_should_unpause_on_focus &&
        Core::GetState(Core::System::GetInstance()) == Core::State::Paused)
    {
      Core::SetState(Core::System::GetInstance(), Core::State::Running);
    }

    m_should_unpause_on_focus = false;

    UpdateCursor();

    // Avoid "race conditions" with message boxes
    if (m_lock_cursor_on_next_activation && !m_waiting_for_message_box)
    {
      if (Settings::Instance().GetLockCursor())
      {
        SetCursorLocked(true);
      }
      m_lock_cursor_on_next_activation = false;
    }

    emit FocusChanged(true);
    break;
  case QEvent::WindowDeactivate:
    SetCursorLocked(false);

    UpdateCursor();

    if (Config::Get(Config::MAIN_PAUSE_ON_FOCUS_LOST) &&
        Core::GetState(Core::System::GetInstance()) == Core::State::Running)
    {
      // If we are declared as the CPU or GPU thread, it means that the real CPU or GPU thread
      // is waiting for us to finish showing a panic alert (with that panic alert likely being
      // the cause of this event), so trying to pause the core would cause a deadlock
      if (!Core::IsCPUThread() && !Core::IsGPUThread())
      {
        m_should_unpause_on_focus = true;
        Core::SetState(Core::System::GetInstance(), Core::State::Paused);
      }
    }

    emit FocusChanged(false);
    break;
  // According to https://bugreports.qt.io/browse/QTBUG-95925 the recommended practice for
  // handling DPI change is responding to paint events
  case QEvent::Paint:
  case QEvent::Resize:
  {
    const QResizeEvent* se = static_cast<QResizeEvent*>(event);
    QSize new_size = se->size();

    QScreen* screen = window()->windowHandle()->screen();

    const float dpr = screen->devicePixelRatio();
    const int width = new_size.width() * dpr;
    const int height = new_size.height() * dpr;

    if (m_last_window_width != width || m_last_window_height != height ||
        m_last_window_scale != dpr)
    {
      m_last_window_width = width;
      m_last_window_height = height;
      m_last_window_scale = dpr;
      emit SizeChanged(width, height);
    }
    break;
  }
  case QEvent::WindowStateChange:
    emit StateChanged(isFullScreen());
    break;
  case QEvent::Close:
    emit Closed();
    break;
  default:
    break;
  }
  return QWidget::event(event);
}

void RenderWidget::PassEventToPresenter(const QEvent* event)
{
  if (!Core::IsRunning(Core::System::GetInstance()))
    return;

  switch (event->type())
  {
  case QEvent::KeyPress:
  case QEvent::KeyRelease:
  {
    // As the imgui KeysDown array is only 512 elements wide, and some Qt keys which
    // we need to track (e.g. alt) are above this value, we mask the lower 9 bits.
    // Even masked, the key codes are still unique, so conflicts aren't an issue.
    // The actual text input goes through AddInputCharactersUTF8().
    const QKeyEvent* key_event = static_cast<const QKeyEvent*>(event);
    const bool is_down = event->type() == QEvent::KeyPress;
    const u32 key = static_cast<u32>(key_event->key() & 0x1FF);

    const char* chars = nullptr;
    QByteArray utf8;

    if (is_down)
    {
      utf8 = key_event->text().toUtf8();

      if (utf8.size())
        chars = utf8.constData();
    }

    // Pass the key onto Presenter (for the imgui UI)
    g_presenter->SetKey(key, is_down, chars);
  }
  break;

  case QEvent::MouseMove:
  {
    // Qt multiplies all coordinates by the scaling factor in highdpi mode, giving us "scaled" mouse
    // coordinates (as if the screen was standard dpi). We need to update the mouse position in
    // native coordinates, as the UI (and game) is rendered at native resolution.
    const float scale = devicePixelRatio();
    float x = static_cast<const QMouseEvent*>(event)->pos().x() * scale;
    float y = static_cast<const QMouseEvent*>(event)->pos().y() * scale;

    g_presenter->SetMousePos(x, y);
  }
  break;

  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonRelease:
  {
    const u32 button_mask = static_cast<u32>(static_cast<const QMouseEvent*>(event)->buttons());
    g_presenter->SetMousePress(button_mask);
  }
  break;

  default:
    break;
  }
}

void RenderWidget::SetPresenterKeyMap()
{
  static constexpr DolphinKeyMap key_map = {
      Qt::Key_Tab,    Qt::Key_Left,      Qt::Key_Right, Qt::Key_Up,     Qt::Key_Down,
      Qt::Key_PageUp, Qt::Key_PageDown,  Qt::Key_Home,  Qt::Key_End,    Qt::Key_Insert,
      Qt::Key_Delete, Qt::Key_Backspace, Qt::Key_Space, Qt::Key_Return, Qt::Key_Escape,
      Qt::Key_Enter,  // Keypad enter
      Qt::Key_A,      Qt::Key_C,         Qt::Key_V,     Qt::Key_X,      Qt::Key_Y,
      Qt::Key_Z,
  };

  g_presenter->SetKeyMap(key_map);
}
