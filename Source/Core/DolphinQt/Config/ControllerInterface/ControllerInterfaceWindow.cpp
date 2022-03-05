// Copyright 2019 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/ControllerInterface/ControllerInterfaceWindow.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWindow>

#if defined(CIFACE_USE_DUALSHOCKUDPCLIENT)
#include "DolphinQt/Config/ControllerInterface/DualShockUDPClientWidget.h"
#endif

#include "Common/Logging/Log.h"

class OpenGLWindow : public QWindow
{
public:
  explicit OpenGLWindow(QWindow* parent = nullptr) : QWindow{parent}
  {
    setSurfaceType(QWindow::OpenGLSurface);
    QSurfaceFormat format;
    format.setSamples(1);

    setFormat(format);
    resize(640, 480);
  }

  ~OpenGLWindow()
  {
    delete m_gl_funcs;
    delete m_device;
  }

  void render()
  {
    if (!m_device)
      m_device = new QOpenGLPaintDevice;

    m_gl_funcs->glClearColor(0, 0, 0, 0);
    m_gl_funcs->glClear(GL_COLOR_BUFFER_BIT);

    m_device->setSize(size() * devicePixelRatio());
    m_device->setDevicePixelRatio(devicePixelRatio());
  }

  void renderLater() { requestUpdate(); }
  void renderNow()
  {
    if (!isExposed())
      return;

    bool need_to_init = false;

    if (!m_context)
    {
      m_context = new QOpenGLContext{this};
      m_context->setFormat(requestedFormat());
      m_context->create();
      need_to_init = true;
    }

    m_context->makeCurrent(this);

    if (need_to_init)
    {
      m_gl_funcs = new QOpenGLFunctions{m_context};

      ERROR_LOG(CONTROLLERINTERFACE, "pre init gl");
      // m_gl_funcs->initializeOpenGLFunctions();
      ERROR_LOG(CONTROLLERINTERFACE, "post init gl");
    }

    render();

    m_context->swapBuffers(this);
    renderLater();
  }

protected:
  bool event(QEvent* event) override
  {
    switch (event->type())
    {
    case QEvent::UpdateRequest:
      renderNow();
      return true;
    default:
      return QWindow::event(event);
    }
  }

  void exposeEvent(QExposeEvent*) override
  {
    if (isExposed())
      renderNow();
  }

private:
  QOpenGLFunctions* m_gl_funcs = nullptr;
  QOpenGLContext* m_context = nullptr;
  QOpenGLPaintDevice* m_device = nullptr;
};

ControllerInterfaceWindow::ControllerInterfaceWindow(QWidget* parent) : QDialog(parent)
{
  CreateMainLayout();

  setWindowTitle(tr("Alternate Input Sources"));
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

void ControllerInterfaceWindow::CreateMainLayout()
{
  m_button_box = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(m_button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

  m_tab_widget = new QTabWidget();
#if defined(CIFACE_USE_DUALSHOCKUDPCLIENT)
  m_dsuclient_widget = new DualShockUDPClientWidget();
  m_tab_widget->addTab(m_dsuclient_widget, tr("DSU Client"));  // TODO: use GetWrappedWidget()?
#endif

  // Steam Input
  {
    const auto steam_input_tab = new QWidget{m_tab_widget};
    m_tab_widget->addTab(steam_input_tab, tr("Steam Input"));
    const auto layout = new QVBoxLayout{steam_input_tab};

    const auto btn = new QPushButton{tr("Open Steam Overlay Input Config"), steam_input_tab};
    layout->addWidget(btn);

    connect(btn, &QPushButton::clicked, []() {
      const auto window = new OpenGLWindow(nullptr);
      window->show();
    });
  }

  auto* main_layout = new QVBoxLayout();
  if (m_tab_widget->count() > 0)
  {
    main_layout->addWidget(m_tab_widget);
  }
  else
  {
    main_layout->addWidget(new QLabel(tr("Nothing to configure")), 0,
                           Qt::AlignVCenter | Qt::AlignHCenter);
  }
  main_layout->addWidget(m_button_box);
  setLayout(main_layout);
}
