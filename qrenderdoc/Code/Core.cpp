#include "Core.h"
#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaObject>
#include <QTimer>
#include "Windows/MainWindow.h"

Core::Core(QString paramFilename, QString remoteHost, uint32_t remoteIdent, bool temp)
{
  m_LogLoaded = false;
  m_LoadInProgress = false;

  m_EventID = 0;

  memset(&m_APIProps, 0, sizeof(m_APIProps));

  qApp->setApplicationVersion(RENDERDOC_GetVersionString());

  m_MainWindow = new MainWindow(this);
  m_MainWindow->show();

  if(!paramFilename.isEmpty())
  {
    QFileInfo fi(paramFilename);

    if(fi.suffix() == "rdc")
    {
      LoadLogfile(paramFilename, temp);
    }
  }
}

Core::~Core()
{
  delete m_MainWindow;
}

void Core::LoadLogfile(QString logFile, bool temporary)
{
  LoadLogfile(-1, "", logFile, temporary);
}

void Core::LoadLogfile(int proxyRenderer, QString replayHost, QString logFile, bool temporary)
{
  m_LogFile = logFile;

  m_LoadInProgress = true;

  float loadProgress = 0.0f;
  float postloadProgress = 0.0f;

  // this function call will block until the log is either loaded, or there's some failure
  m_Renderer.Init(proxyRenderer, replayHost, logFile, &loadProgress);

  // if the renderer isn't running, we hit a failure case so display an error message
  if(!m_Renderer.IsRunning())
  {
    QString errmsg = "Unknown error message";
    ReplayCreateStatus status = m_Renderer.GetCreateStatus();
    errmsg = status;

    if(proxyRenderer >= 0)
      QMessageBox::critical(NULL, "Error opening log",
                            QString("%1\nFailed to transfer and replay on remote host %2: %3.\n\n"
                                    "Check diagnostic log in Help menu for more details.")
                                .arg(logFile, replayHost, errmsg));
    else
      QMessageBox::critical(NULL, "Error opening log",
                            QString("%1\nFailed to open logfile for replay: %1.\n\n"
                                    "Check diagnostic log in Help menu for more details.")
                                .arg(logFile, errmsg));

    m_LoadInProgress = false;

    return;
  }

  m_EventID = 0;

  // fetch initial data like drawcalls, textures and buffers
  m_Renderer.BlockInvoke([this, &postloadProgress](IReplayRenderer *r) {
    r->GetFrameInfo(&m_FrameInfo);

    m_APIProps = r->GetAPIProperties();

    postloadProgress = 0.2f;

    r->GetDrawcalls(&m_Drawcalls);

    postloadProgress = 0.4f;

    r->GetBuffers(&m_BufferList);
    for(int i = 0; i < m_BufferList.count; i++)
      m_Buffers[m_BufferList[i].ID] = &m_BufferList[i];

    postloadProgress = 0.8f;

    r->GetTextures(&m_TextureList);
    for(int i = 0; i < m_TextureList.count; i++)
      m_Textures[m_TextureList[i].ID] = &m_TextureList[i];

    postloadProgress = 0.9f;

    r->GetD3D11PipelineState(&CurD3D11PipelineState);
    r->GetGLPipelineState(&CurGLPipelineState);
    r->GetVulkanPipelineState(&CurVulkanPipelineState);
    // CurPipelineState.SetStates(m_APIProps, CurD3D11PipelineState, CurGLPipelineState);

    UnreadMessageCount = 0;
    AddMessages(m_FrameInfo.debugMessages);

    postloadProgress = 1.0f;
  });

  QThread::msleep(20);

  m_LogLoaded = true;

  QList<ILogViewerForm *> logviewers(m_LogViewers);

  GUIInvoke::blockcall([&logviewers]() {
    // notify all the registers log viewers that a log has been loaded
    for(ILogViewerForm *logviewer : logviewers)
    {
      if(logviewer)
        logviewer->OnLogfileLoaded();
    }
  });

  m_LoadInProgress = false;
}

void Core::SetEventID(ILogViewerForm *exclude, uint32_t eventID)
{
  m_EventID = eventID;

  m_Renderer.BlockInvoke([eventID, this](IReplayRenderer *r) {
    r->SetFrameEvent(eventID, false);
    r->GetD3D11PipelineState(&CurD3D11PipelineState);
    r->GetGLPipelineState(&CurGLPipelineState);
    r->GetVulkanPipelineState(&CurVulkanPipelineState);
    // CurPipelineState.SetStates(m_APIProps, CurD3D11PipelineState, CurGLPipelineState);
  });

  for(ILogViewerForm *logviewer : m_LogViewers)
  {
    if(logviewer == exclude)
      continue;

    logviewer->OnEventSelected(eventID);
  }
}

void GUIInvoke::call(const std::function<void()> &f)
{
  if(qApp->thread() == QThread::currentThread())
  {
    f();
    return;
  }

  // TODO: could maybe do away with string compare here via caching
  // invoke->metaObject()->indexOfMethod("doInvoke"); ?

  GUIInvoke *invoke = new GUIInvoke(f);
  invoke->moveToThread(qApp->thread());
  QMetaObject::invokeMethod(invoke, "doInvoke", Qt::QueuedConnection);
}

void GUIInvoke::blockcall(const std::function<void()> &f)
{
  if(qApp->thread() == QThread::currentThread())
  {
    f();
    return;
  }

  // TODO: could maybe do away with string compare here via caching
  // invoke->metaObject()->indexOfMethod("doInvoke"); ?

  GUIInvoke *invoke = new GUIInvoke(f);
  invoke->moveToThread(qApp->thread());
  QMetaObject::invokeMethod(invoke, "doInvoke", Qt::BlockingQueuedConnection);
}
