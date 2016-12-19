/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Baldur Karlsson
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "CaptureContext.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTimer>
#include "Windows/APIInspector.h"
#include "Windows/BufferViewer.h"
#include "Windows/Dialogs/CaptureDialog.h"
#include "Windows/Dialogs/LiveCapture.h"
#include "Windows/EventBrowser.h"
#include "Windows/MainWindow.h"
#include "Windows/PipelineState/PipelineStateViewer.h"
#include "Windows/TextureViewer.h"
#include "QRDUtils.h"

CaptureContext::CaptureContext(QString paramFilename, QString remoteHost, uint32_t remoteIdent,
                               bool temp, PersistantConfig &cfg)
    : Config(cfg)
{
  m_LogLoaded = false;
  m_LoadInProgress = false;

  m_EventID = 0;

  memset(&m_APIProps, 0, sizeof(m_APIProps));

  qApp->setApplicationVersion(RENDERDOC_GetVersionString());

  m_Icon = new QIcon();
  m_Icon->addFile(QStringLiteral(":/Resources/icon.ico"), QSize(), QIcon::Normal, QIcon::Off);

  m_MainWindow = new MainWindow(this);
  m_MainWindow->show();

  if(remoteIdent != 0)
  {
    m_MainWindow->ShowLiveCapture(
        new LiveCapture(this, remoteHost, remoteIdent, m_MainWindow, m_MainWindow));
  }

  if(!paramFilename.isEmpty())
  {
    QFileInfo fi(paramFilename);

    m_MainWindow->LoadFromFilename(paramFilename);
  }
}

CaptureContext::~CaptureContext()
{
  delete m_Icon;
  m_Renderer.CloseThread();
  delete m_MainWindow;
}

bool CaptureContext::isRunning()
{
  return m_MainWindow && m_MainWindow->isVisible();
}

QString CaptureContext::ConfigFile(const QString &filename)
{
  QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

  QDir dir(path);
  if(!dir.exists())
    dir.mkdir(".");

  return QDir::cleanPath(dir.absoluteFilePath(filename));
}

QString CaptureContext::TempLogFilename(QString appname)
{
  QString folder = Config.TemporaryCaptureDirectory;

  QDir dir(folder);

  if(folder == "" || !dir.exists())
  {
    dir = QDir(QDir::tempPath());

    dir.mkdir("RenderDoc");

    dir = QDir(dir.absoluteFilePath("RenderDoc"));
  }

  return dir.absoluteFilePath(
      appname + "_" + QDateTime::currentDateTimeUtc().toString("yyyy.MM.dd_HH.mm.ss") + ".rdc");
}

void CaptureContext::LoadLogfile(const QString &logFile, const QString &origFilename,
                                 bool temporary, bool local)
{
  m_Progress = new QProgressDialog(QString("Loading Log"), QString(), 0, 1000, m_MainWindow);
  m_Progress->setWindowTitle("Please Wait");
  m_Progress->setWindowFlags(Qt::CustomizeWindowHint | Qt::Dialog | Qt::WindowTitleHint);
  m_Progress->setWindowIcon(QIcon());
  m_Progress->setMinimumSize(QSize(250, 0));
  m_Progress->setMaximumSize(QSize(250, 10000));
  m_Progress->setCancelButton(NULL);
  m_Progress->setMinimumDuration(0);
  m_Progress->setWindowModality(Qt::ApplicationModal);
  m_Progress->setValue(0);

  QLabel *label = new QLabel(m_Progress);

  label->setText(QString("Loading Log: %1").arg(origFilename));
  label->setAlignment(Qt::AlignCenter);
  label->setWordWrap(true);

  m_Progress->setLabel(label);

  LambdaThread *thread = new LambdaThread([this, logFile, origFilename, temporary, local]() {
    LoadLogfileThreaded(logFile, origFilename, temporary, local);

    GUIInvoke::call([this, origFilename]() {
      delete m_Progress;
      m_Progress = NULL;
    });
  });
  thread->selfDelete(true);
  thread->start();
}

void CaptureContext::LoadLogfileThreaded(const QString &logFile, const QString &origFilename,
                                         bool temporary, bool local)
{
  QFileInfo fi(ConfigFile("UI.config"));

  m_LogFile = origFilename;

  m_LogLocal = local;

  m_LoadInProgress = true;

  if(fi.exists())
    Config.Serialize(fi.absoluteFilePath());

  float loadProgress = 0.0f;
  float postloadProgress = 0.0f;

  QSemaphore progressThread(1);

  LambdaThread progressTickerThread([this, &progressThread, &loadProgress, &postloadProgress]() {
    while(progressThread.available())
    {
      QThread::msleep(30);

      float val = 0.8f * loadProgress + 0.19f * postloadProgress + 0.01f;

      GUIInvoke::call([this, val]() {
        m_Progress->setValue(val * 1000);
        m_MainWindow->setProgress(val);
      });
    }
    GUIInvoke::call([this]() { m_Progress->setValue(1000); });
  });
  progressTickerThread.start();

  // this function call will block until the log is either loaded, or there's some failure
  m_Renderer.OpenCapture(logFile, &loadProgress);

  // if the renderer isn't running, we hit a failure case so display an error message
  if(!m_Renderer.IsRunning())
  {
    QString errmsg = "Unknown error message";
    errmsg = ToQStr(m_Renderer.GetCreateStatus());

    progressThread.acquire();
    progressTickerThread.wait();

    RDDialog::critical(NULL, "Error opening log",
                       QString("%1\nFailed to open logfile for replay: %2.\n\n"
                               "Check diagnostic log in Help menu for more details.")
                           .arg(logFile)
                           .arg(errmsg));

    GUIInvoke::call([this]() {
      m_Progress->setValue(1000);
      m_MainWindow->setProgress(-1.0f);
      m_Progress->hide();
    });

    m_LoadInProgress = false;

    return;
  }

  if(!temporary)
  {
    PersistantConfig::AddRecentFile(Config.RecentLogFiles, origFilename, 10);

    if(fi.exists())
      Config.Serialize(fi.absoluteFilePath());
  }

  m_EventID = 0;

  // fetch initial data like drawcalls, textures and buffers
  m_Renderer.BlockInvoke([this, &postloadProgress](IReplayRenderer *r) {
    r->GetFrameInfo(&m_FrameInfo);

    m_APIProps = r->GetAPIProperties();

    postloadProgress = 0.2f;

    r->GetDrawcalls(&m_Drawcalls);

    postloadProgress = 0.4f;

    r->GetSupportedWindowSystems(&m_WinSystems);

#if defined(RENDERDOC_PLATFORM_WIN32)
    m_CurWinSystem = eWindowingSystem_Win32;
#elif defined(RENDERDOC_PLATFORM_LINUX)
    m_CurWinSystem = eWindowingSystem_Xlib;

    // prefer XCB, if supported
    for(WindowingSystem sys : m_WinSystems)
    {
      if(sys == eWindowingSystem_XCB)
      {
        m_CurWinSystem = eWindowingSystem_XCB;
        break;
      }
    }

    if(m_CurWinSystem == eWindowingSystem_XCB)
      m_XCBConnection = QX11Info::connection();
    else
      m_X11Display = QX11Info::display();
#endif

    r->GetBuffers(&m_BufferList);
    for(FetchBuffer &b : m_BufferList)
      m_Buffers[b.ID] = &b;

    postloadProgress = 0.8f;

    r->GetTextures(&m_TextureList);
    for(FetchTexture &t : m_TextureList)
      m_Textures[t.ID] = &t;

    postloadProgress = 0.9f;

    r->GetD3D11PipelineState(&CurD3D11PipelineState);
    r->GetD3D12PipelineState(&CurD3D12PipelineState);
    r->GetGLPipelineState(&CurGLPipelineState);
    r->GetVulkanPipelineState(&CurVulkanPipelineState);
    CurPipelineState.SetStates(m_APIProps, &CurD3D11PipelineState, &CurD3D12PipelineState,
                               &CurGLPipelineState, &CurVulkanPipelineState);

    UnreadMessageCount = 0;
    AddMessages(m_FrameInfo.debugMessages);

    postloadProgress = 1.0f;
  });

  QThread::msleep(20);

  QDateTime today = QDateTime::currentDateTimeUtc();
  QDateTime compare = today.addDays(-21);

  if(compare > Config.DegradedLog_LastUpdate && m_APIProps.degraded)
  {
    Config.DegradedLog_LastUpdate = today;

    RDDialog::critical(
        NULL, "Degraded support of log",
        QString(
            "%1\nThis log opened with degraded support - "
            "this could mean missing hardware support caused a fallback to software rendering.\n\n"
            "This warning will not appear every time this happens, "
            "check debug errors/warnings window for more details.")
            .arg(origFilename));
  }

  m_LogLoaded = true;

  progressThread.acquire();
  progressTickerThread.wait();

  QVector<ILogViewerForm *> logviewers(m_LogViewers);

  GUIInvoke::blockcall([&logviewers]() {
    // notify all the registers log viewers that a log has been loaded
    for(ILogViewerForm *logviewer : logviewers)
    {
      if(logviewer)
        logviewer->OnLogfileLoaded();
    }
  });

  m_LoadInProgress = false;

  GUIInvoke::call([this]() {
    m_Progress->setValue(1000);
    m_MainWindow->setProgress(1.0f);
    m_Progress->hide();
  });
}

void CaptureContext::CloseLogfile()
{
  if(!m_LogLoaded)
    return;

  m_LogFile = "";

  m_Renderer.CloseThread();

  memset(&m_APIProps, 0, sizeof(m_APIProps));
  memset(&m_FrameInfo, 0, sizeof(m_FrameInfo));
  m_Buffers.clear();
  m_BufferList.clear();
  m_Textures.clear();
  m_TextureList.clear();

  CurD3D11PipelineState = D3D11PipelineState();
  CurD3D12PipelineState = D3D12PipelineState();
  CurGLPipelineState = GLPipelineState();
  CurVulkanPipelineState = VulkanPipelineState();
  CurPipelineState.SetStates(m_APIProps, NULL, NULL, NULL, NULL);

  DebugMessages.clear();
  UnreadMessageCount = 0;

  m_LogLoaded = false;

  QVector<ILogViewerForm *> logviewers(m_LogViewers);

  for(ILogViewerForm *logviewer : logviewers)
  {
    if(logviewer)
      logviewer->OnLogfileClosed();
  }
}

void CaptureContext::SetEventID(ILogViewerForm *exclude, uint32_t eventID, bool force)
{
  m_EventID = eventID;

  m_Renderer.BlockInvoke([this, eventID, force](IReplayRenderer *r) {
    r->SetFrameEvent(eventID, force);
    r->GetD3D11PipelineState(&CurD3D11PipelineState);
    r->GetD3D12PipelineState(&CurD3D12PipelineState);
    r->GetGLPipelineState(&CurGLPipelineState);
    r->GetVulkanPipelineState(&CurVulkanPipelineState);
    CurPipelineState.SetStates(m_APIProps, &CurD3D11PipelineState, &CurD3D12PipelineState,
                               &CurGLPipelineState, &CurVulkanPipelineState);
  });

  for(ILogViewerForm *logviewer : m_LogViewers)
  {
    if(logviewer == exclude)
      continue;

    logviewer->OnEventSelected(eventID);
  }
}

void *CaptureContext::FillWindowingData(WId widget)
{
#if defined(WIN32)

  return (void *)widget;

#elif defined(RENDERDOC_PLATFORM_LINUX)

  static XCBWindowData xcb;
  static XlibWindowData xlib;

  if(m_CurWinSystem == eWindowingSystem_XCB)
  {
    xcb.connection = m_XCBConnection;
    xcb.window = (xcb_window_t)widget;
    return &xcb;
  }
  else
  {
    xlib.display = m_X11Display;
    xlib.window = (Drawable)widget;
    return &xlib;
  }

#elif defined(RENDERDOC_PLATFORM_APPLE)

  return (void *)widget;

#else

#error "Unknown platform"

#endif
}

EventBrowser *CaptureContext::eventBrowser()
{
  if(m_EventBrowser)
    return m_EventBrowser;

  m_EventBrowser = new EventBrowser(this, m_MainWindow);
  m_EventBrowser->setObjectName("eventBrowser");
  m_EventBrowser->setWindowIcon(*m_Icon);

  return m_EventBrowser;
}

APIInspector *CaptureContext::apiInspector()
{
  if(m_APIInspector)
    return m_APIInspector;

  m_APIInspector = new APIInspector(this, m_MainWindow);
  m_APIInspector->setObjectName("apiInspector");
  m_APIInspector->setWindowIcon(*m_Icon);

  return m_APIInspector;
}

TextureViewer *CaptureContext::textureViewer()
{
  if(m_TextureViewer)
    return m_TextureViewer;

  m_TextureViewer = new TextureViewer(this, m_MainWindow);
  m_TextureViewer->setObjectName("textureViewer");
  m_TextureViewer->setWindowIcon(*m_Icon);

  return m_TextureViewer;
}

BufferViewer *CaptureContext::meshPreview()
{
  if(m_MeshPreview)
    return m_MeshPreview;

  m_MeshPreview = new BufferViewer(this, m_MainWindow);
  m_MeshPreview->setObjectName("meshPreview");
  m_MeshPreview->setWindowIcon(*m_Icon);

  return m_MeshPreview;
}

PipelineStateViewer *CaptureContext::pipelineViewer()
{
  if(m_PipelineViewer)
    return m_PipelineViewer;

  m_PipelineViewer = new PipelineStateViewer(this, m_MainWindow);
  m_PipelineViewer->setObjectName("pipelineViewer");
  m_PipelineViewer->setWindowIcon(*m_Icon);

  return m_PipelineViewer;
}

CaptureDialog *CaptureContext::captureDialog()
{
  if(m_CaptureDialog)
    return m_CaptureDialog;

  m_CaptureDialog = new CaptureDialog(
      this,
      [this](const QString &exe, const QString &workingDir, const QString &cmdLine,
             const QList<EnvironmentModification> &env, CaptureOptions opts) {
        return m_MainWindow->OnCaptureTrigger(exe, workingDir, cmdLine, env, opts);
      },
      [this](uint32_t PID, const QList<EnvironmentModification> &env, const QString &name,
             CaptureOptions opts) { return m_MainWindow->OnInjectTrigger(PID, env, name, opts); },
      m_MainWindow);
  m_CaptureDialog->setObjectName("capDialog");
  m_CaptureDialog->setWindowIcon(*m_Icon);

  return m_CaptureDialog;
}

void CaptureContext::showEventBrowser()
{
  m_MainWindow->showEventBrowser();
}

void CaptureContext::showAPIInspector()
{
  m_MainWindow->showAPIInspector();
}

void CaptureContext::showTextureViewer()
{
  m_MainWindow->showTextureViewer();
}

void CaptureContext::showMeshPreview()
{
  m_MainWindow->showMeshPreview();
}

void CaptureContext::showPipelineViewer()
{
  m_MainWindow->showPipelineViewer();
}

void CaptureContext::showCaptureDialog()
{
  m_MainWindow->showCaptureDialog();
}

QWidget *CaptureContext::createToolWindow(const QString &objectName)
{
  if(objectName == "textureViewer")
  {
    return textureViewer();
  }
  else if(objectName == "eventBrowser")
  {
    return eventBrowser();
  }
  else if(objectName == "pipelineViewer")
  {
    return pipelineViewer();
  }
  else if(objectName == "meshPreview")
  {
    return meshPreview();
  }
  else if(objectName == "apiInspector")
  {
    return apiInspector();
  }
  else if(objectName == "capDialog")
  {
    return captureDialog();
  }

  return NULL;
}

void CaptureContext::windowClosed(QWidget *window)
{
  if((QWidget *)m_EventBrowser == window)
    m_EventBrowser = NULL;
  else if((QWidget *)m_TextureViewer == window)
    m_TextureViewer = NULL;
  else if((QWidget *)m_CaptureDialog == window)
    m_CaptureDialog = NULL;
  else if((QWidget *)m_APIInspector == window)
    m_APIInspector = NULL;
  else if((QWidget *)m_PipelineViewer == window)
    m_PipelineViewer = NULL;
  else if((QWidget *)m_MeshPreview == window)
    m_MeshPreview = NULL;
  else
    qCritical() << "Unrecognised window being closed: " << window;
}
